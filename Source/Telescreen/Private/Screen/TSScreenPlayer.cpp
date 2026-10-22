#include "Screen/TSScreenPlayer.h"
#include "Buildables/TSBuildableScreen.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "FGGameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/Texture2DResource.h"
#include "Screen/TSStreamingSoundWave.h"
#include "Audio/TSWwiseScreens.h"
#include "Screen/TSRemoteConnections.h"
#include "Telescreen.h"
#include "TSConfig.h"
#include "FGPlayerController.h"
#include "Engine/NetConnection.h"
#include "Net/TSNetRate.h"
#include "TSRemoteCallObject.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "UI/TSScreenWindow.h"

#define LOCTEXT_NAMESPACE "Telescreen"

using TSHostProtocol::ECommand;
using TSHostProtocol::EInput;
using TSHostProtocol::EKeyType;
using TSHostProtocol::ESessionState;

namespace
{
	TArray<TWeakObjectPtr<UTSScreenPlayer>> GPlayers;

	constexpr int32 StreamChunkBytes = 768;
	constexpr int32 MaxStreamChunks = 1024;
	// Upper bound for sending queued stream data, below the stream net rate so gameplay traffic keeps room.
	constexpr double StreamPaceBytesPerSecond = 750000.0;
	// How long the oldest queued chunk may wait before the bitrate is lowered, or the queue dropped.
	constexpr double QueueLowerSeconds = 0.4;
	constexpr double QueueDropSeconds = 1.0;
	// How long a viewer waits for a requested keyframe before asking again.
	constexpr double KeyframeRetrySeconds = 0.75;

	constexpr double SyncInterval = 0.25;
	constexpr double RankInterval = 1.0;
	constexpr double AudioInterval = 0.5;
	constexpr int32 StatusWidth = 1024;

	FString ItemKey(int32 Index, const FTSMediaItem& Item)
	{
		return FString::Printf(TEXT("%d|%s"), Index, *Item.Url);
	}

	float ReadMasterVolume()
	{
		const UFGGameUserSettings* Settings = UFGGameUserSettings::GetFGGameUserSettings();
		if (!Settings)
		{
			return 1.f;
		}
		static bool bReported = false;
		if (bReported)
		{
			return 1.f;
		}
		const FVariant Raw = Settings->GetOptionValue(TEXT("RTPC.Menu_Volume_Master"));
		float Value;
		switch (Raw.GetType())
		{
		case EVariantTypes::Float: Value = Raw.GetValue<float>(); break;
		case EVariantTypes::Double: Value = float(Raw.GetValue<double>()); break;
		case EVariantTypes::Int32: Value = float(Raw.GetValue<int32>()); break;
		default:
			bReported = true;
			UE_LOG(LogTelescreen, Warning, TEXT("Master volume option unreadable (variant type %d); using full volume"), int32(Raw.GetType()));
			return 1.f;
		}
		return FMath::Clamp(Value > 1.f ? Value / 100.f : Value, 0.f, 1.f);
	}

	// Lays out a Width x Height picture on a screen of aspect ScreenAspect: texture size, source rectangle, destination offset.
	struct FFit
	{
		int32 TexWidth, TexHeight;
		int32 SrcX, SrcY, SrcWidth, SrcHeight;
		int32 DstX, DstY;
	};

	FFit ComputeFit(int32 Width, int32 Height, double ScreenAspect, ETSAspectMode Mode)
	{
		FFit F{Width, Height, 0, 0, Width, Height, 0, 0};
		const double VideoAspect = double(Width) / double(FMath::Max(1, Height));
		if (Mode == ETSAspectMode::Stretch || FMath::IsNearlyEqual(VideoAspect, ScreenAspect, 0.01))
		{
			return F;
		}
		const bool bScreenWider = ScreenAspect > VideoAspect;
		if (Mode == ETSAspectMode::Letterbox)
		{
			if (bScreenWider)
			{
				F.TexWidth = FMath::Max(2, int32(FMath::RoundToInt(Height * ScreenAspect)) & ~1);
				F.DstX = (F.TexWidth - Width) / 2;
			}
			else
			{
				F.TexHeight = FMath::Max(2, int32(FMath::RoundToInt(Width / ScreenAspect)) & ~1);
				F.DstY = (F.TexHeight - Height) / 2;
			}
		}
		else
		{
			if (bScreenWider)
			{
				F.TexHeight = F.SrcHeight = FMath::Clamp(int32(FMath::RoundToInt(Width / ScreenAspect)) & ~1, 2, Height);
				F.SrcY = (Height - F.SrcHeight) / 2;
			}
			else
			{
				F.TexWidth = F.SrcWidth = FMath::Clamp(int32(FMath::RoundToInt(Height * ScreenAspect)) & ~1, 2, Width);
				F.SrcX = (Width - F.SrcWidth) / 2;
			}
		}
		return F;
	}
}

// Receives the stream sender session's messages and forwards them to the player.
class FTSStreamSenderListener : public ITSHostSessionListener
{
public:
	explicit FTSStreamSenderListener(UTSScreenPlayer* InOwner) : Owner(InOwner) {}

	virtual void OnHostVideo(uint32, uint32, uint32, const FString&) override {}
	virtual void OnHostAudio(uint32, uint32, uint32, const FString&) override {}
	virtual void OnHostState(ESessionState State, double, double, const FString& Message) override
	{
		if (Owner.IsValid() && (State == ESessionState::Playing || State == ESessionState::Error))
		{
			Owner->OnSenderState(Message);
		}
	}
	virtual void OnHostLost() override
	{
		if (Owner.IsValid())
		{
			Owner->OnSenderLost();
		}
	}
	virtual void OnHostStreamPacket(uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, TArray<uint8>&& Data) override
	{
		if (Owner.IsValid())
		{
			Owner->SendStreamChunks(Kind, Flags, PtsMs, Codec, Data);
		}
	}

private:
	TWeakObjectPtr<UTSScreenPlayer> Owner;
};

UTSScreenPlayer::UTSScreenPlayer()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

ATSBuildableScreen* UTSScreenPlayer::GetScreen() const
{
	return Cast<ATSBuildableScreen>(GetOwner());
}

void UTSScreenPlayer::BeginPlay()
{
	Super::BeginPlay();
	GPlayers.Add(this);
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	ScreenChangedHandle = Screen->OnScreenChanged.AddUObject(this, &UTSScreenPlayer::OnScreenChanged);
	ChunkHandle = Screen->OnStreamChunk.AddUObject(this, &UTSScreenPlayer::OnStreamChunk);
	KeyframeHandle = Screen->OnKeyframeRequested.AddUObject(this, &UTSScreenPlayer::OnKeyframeRequested);

	const FVector2D Size = Screen->GetScreenWorldSize();
	const double Aspect = Size.Y > 1.0 ? Size.X / Size.Y : 2.0;
	StatusTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), StatusWidth,
		FMath::Clamp(int32(StatusWidth / Aspect), 64, 2048));
	if (StatusTarget)
	{
		StatusTarget->ClearColor = FLinearColor(0.01f, 0.012f, 0.02f, 1.f);
		StatusTarget->OnCanvasRenderTargetUpdate.AddDynamic(this, &UTSScreenPlayer::DrawStatus);
	}
	RankActivity();
	Reconcile();
}

void UTSScreenPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPinned)
	{
		bPinned = false;
		UTSScreenWindow::Close();
	}
	CloseSession();
	GPlayers.RemoveAll([this](const TWeakObjectPtr<UTSScreenPlayer>& P) { return !P.IsValid() || P.Get() == this; });
	if (ATSBuildableScreen* Screen = GetScreen())
	{
		Screen->OnScreenChanged.Remove(ScreenChangedHandle);
		Screen->OnStreamChunk.Remove(ChunkHandle);
		Screen->OnKeyframeRequested.Remove(KeyframeHandle);
	}
	if (Audio)
	{
		Audio->Stop();
		Audio->DestroyComponent();
		Audio = nullptr;
	}
	StopWwise();
	if (WwiseEmitter)
	{
		WwiseEmitter->DestroyComponent();
		WwiseEmitter = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UTSScreenPlayer::OnScreenChanged()
{
	Reconcile();
	if (!bShowingVideo)
	{
		ShowStatus();
	}
}

bool UTSScreenPlayer::ShouldLoad(const FTSMediaItem& Item, FText& OutReason) const
{
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	const FString MyName = Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
	if (!MyName.IsEmpty() && Item.AddedBy == MyName)
	{
		return true;
	}
	const bool bLocalFile = !Item.Url.Contains(TEXT("://"));
	switch (ETSAutoLoad(UTSConfig::GetInt(this, UTSConfig::AutoLoadKey, 0)))
	{
	case ETSAutoLoad::Everything:
		return true;
	case ETSAutoLoad::Never:
		OutReason = LOCTEXT("BlockedNever", "Not loaded on this PC: your privacy setting only loads videos you add.");
		return false;
	default:
		if (bLocalFile || UTSConfig::IsAllowedSite(this, Item.Url))
		{
			return true;
		}
		OutReason = LOCTEXT("BlockedSite", "Not loaded on this PC: the site isn't in your allowed list. Open the screen to load it anyway.");
		return false;
	}
}

void UTSScreenPlayer::LoadAnyway()
{
	ATSBuildableScreen* Screen = GetScreen();
	const FTSMediaItem* Item = Screen ? Screen->GetMediaState().GetCurrent() : nullptr;
	if (Item)
	{
		UnblockedKey = ItemKey(Screen->GetMediaState().CurrentIndex, *Item);
		UE_LOG(LogTelescreen, Log, TEXT("%s: loading %s despite the privacy setting"), *Screen->GetName(), *Item->Url);
		Reconcile();
	}
}

void UTSScreenPlayer::Reconcile()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	if (LoopbackSource.IsValid())
	{
		if (Session == 0 && LoopbackSource->HasBrowser())
		{
			if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
			{
				Session = Host->OpenLoopback(LoopbackSource->GetSessionId(), 30, 3000, this);
				SessionKey = TEXT("loopback");
				SetStatus(LOCTEXT("LoopbackTitle", "Loopback test"), LOCTEXT("LoopbackStarting", "Encoding your browser..."));
			}
		}
		return;
	}

	if (Screen->GetMode() != ETSScreenMode::SyncedMedia)
	{
		BlockedReason = FText::GetEmpty();
		if (!Screen->IsLocallyControlled())
		{
			ReconcileViewer(Screen);
			return;
		}
		const ETSScreenMode Mode = Screen->GetMode();
		const FString Key = (Mode == ETSScreenMode::Window ? TEXT("window|") : Mode == ETSScreenMode::RemoteDesktop ? TEXT("remote|") : TEXT("browser|")) +
			Screen->GetSourceUrl();
		WantedKey = Key;
		if (Session != 0 && Key == SessionKey)
		{
			return;
		}
		CloseSession();
		if (Mode == ETSScreenMode::Window)
		{
			OpenWindowSession();
		}
		else if (Mode == ETSScreenMode::RemoteDesktop)
		{
			OpenRemoteSession();
		}
		else
		{
			OpenBrowserSession();
		}
		return;
	}

	const FTSMediaState& State = Screen->GetMediaState();
	const FTSMediaItem* Item = State.GetCurrent();
	if (!Item)
	{
		WantedKey.Reset();
		BlockedReason = FText::GetEmpty();
		CloseSession();
		SetStatus(Screen->GetDisplayName(), LOCTEXT("Idle", "Interact to add a video"));
		return;
	}
	const FString Key = ItemKey(State.CurrentIndex, *Item);
	const FText Title = FText::FromString(Item->GetDisplayTitle());
	FText Reason;
	if (Key != UnblockedKey && !ShouldLoad(*Item, Reason))
	{
		WantedKey.Reset();
		BlockedReason = Reason;
		CloseSession();
		SetStatus(Title, Reason);
		return;
	}
	BlockedReason = FText::GetEmpty();
	if (!bActive)
	{
		WantedKey.Reset();
		CloseSession();
		SetStatus(Title, LOCTEXT("Inactive", "Paused on this PC (far away, or too many screens playing)"));
		return;
	}
	WantedKey = Key;
	if (Session != 0 && Key == SessionKey)
	{
		return;
	}
	CloseSession();
	OpenSession(*Item);
}

void UTSScreenPlayer::OpenSession(const FTSMediaItem& Item)
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	const FText Title = FText::FromString(Item.GetDisplayTitle());
	if (!Host || !Host->EnsureConnected())
	{
		SetStatus(Title, Host ? Host->GetUnavailableReason() : LOCTEXT("NoHost", "Telescreen's player isn't available."));
		return;
	}
	FTSOpenMediaParams Params;
	Params.Url = Item.Url;
	Params.bIsLocalFile = !Item.Url.Contains(TEXT("://"));
	Params.StartSeconds = Screen->GetPlaybackPosition();
	Params.bPaused = Screen->GetMediaState().bPaused;
	Params.MaxHeight = uint32(FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxVideoHeightKey, 720), 144, 2160));
	Params.MaxWidth = Params.MaxHeight * 16 / 9;
	Session = Host->OpenMedia(Params, this);
	if (Session == 0)
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	SessionKey = WantedKey;
	HostState = ESessionState::Resolving;
	NextSync = 0.0;
	SetStatus(Title, LOCTEXT("Loading", "Loading..."));
}

FIntPoint UTSScreenPlayer::ComputeBrowserSize() const
{
	const ATSBuildableScreen* Screen = GetScreen();
	const FVector2D Size = Screen ? Screen->GetScreenWorldSize() : FVector2D(2.0, 1.0);
	const double Aspect = Size.Y > 1.0 ? FMath::Clamp(Size.X / Size.Y, 0.25, 8.0) : 2.0;
	const int32 Height = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxVideoHeightKey, 720), 360, 1440);
	const int32 Width = FMath::Clamp(int32(FMath::RoundToInt(Height * Aspect)) & ~1, 320, 3840);
	return FIntPoint(Width, FMath::Clamp(int32(FMath::RoundToInt(Width / Aspect)) & ~1, 180, 2160));
}

void UTSScreenPlayer::OpenBrowserSession()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	const FText Title = LOCTEXT("YourBrowser", "Your browser");
	if (!Host || !Host->IsBrowserAvailable())
	{
		SetStatus(Title, LOCTEXT("NoBrowserComponent", "The browser component isn't installed."));
		return;
	}
	if (!Host->EnsureConnected())
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	BrowserSize = ComputeBrowserSize();
	FString Url = Screen->GetSourceUrl();
	if (Url.IsEmpty())
	{
		Url = UTSConfig::GetString(this, UTSConfig::BrowserHomeKey, TEXT("https://duckduckgo.com"));
	}
	const bool bKeepLogins = UTSConfig::GetBool(this, UTSConfig::KeepBrowserLoginsKey, false);
	Session = Host->OpenBrowser(Url, BrowserSize.X, BrowserSize.Y, bKeepLogins, TEXT("default"), this);
	if (Session == 0)
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	bBrowserSession = true;
	BrowserState = FTSBrowserState();
	SessionKey = WantedKey;
	SetStatus(Title, LOCTEXT("BrowserStarting", "Starting the browser..."));
}

FIntPoint UTSScreenPlayer::ToSourcePixels(const FVector2D& Position) const
{
	if (ShownTexWidth <= 0 || ShownWidth <= 0)
	{
		return FIntPoint(FMath::Clamp(int32(Position.X * BrowserSize.X), 0, BrowserSize.X - 1), FMath::Clamp(int32(Position.Y * BrowserSize.Y), 0, BrowserSize.Y - 1));
	}
	// Texture pixel -> picture pixel, undoing the letterbox offset or fill crop.
	const int32 X = int32(Position.X * ShownTexWidth) - ShownDstX + ShownSrcX;
	const int32 Y = int32(Position.Y * ShownTexHeight) - ShownDstY + ShownSrcY;
	return FIntPoint(FMath::Clamp(X, 0, ShownWidth - 1), FMath::Clamp(Y, 0, ShownHeight - 1));
}

FText UTSScreenPlayer::GetControllerTitle() const
{
	const ATSBuildableScreen* Screen = GetScreen();
	const FText Name = FText::FromString(Screen ? Screen->GetControllerName() : FString());
	if (Screen && Screen->GetMode() == ETSScreenMode::Window)
	{
		return FText::Format(LOCTEXT("OtherWindow", "{0} is sharing a window"), Name);
	}
	if (Screen && Screen->GetMode() == ETSScreenMode::RemoteDesktop)
	{
		return FText::Format(LOCTEXT("OtherRemote", "{0} is showing a remote desktop"), Name);
	}
	return FText::Format(LOCTEXT("OtherBrowser", "{0} is using the browser"), Name);
}

void UTSScreenPlayer::OpenRemoteSession()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	const FText Title = LOCTEXT("YourRemote", "Your remote desktop");
	if (!Host || !Host->EnsureConnected())
	{
		SetStatus(Title, Host ? Host->GetUnavailableReason() : LOCTEXT("NoHostRemote", "Telescreen's player isn't available."));
		return;
	}
	// The address and password stay on this PC; the screen only carries the token the panel made.
	const FTSRemoteConnection* Connection = TSRemoteConnections::FindPending(Screen->GetSourceUrl());
	if (!Connection)
	{
		SetStatus(Title, LOCTEXT("RemoteGone", "Connect again from the screen's panel."));
		if (UTSRemoteCallObject* RCO = FindRemoteCallObject())
		{
			RCO->Server_ReleaseControl(Screen);
		}
		return;
	}
	Session = Host->OpenVnc(Connection->Host, Connection->Port, Connection->Password, 1920, 1080, true, this);
	if (Session == 0)
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	bRemoteSession = true;
	SessionKey = WantedKey;
	SetStatus(Title, FText::Format(LOCTEXT("RemoteConnecting", "Connecting to {0}..."), FText::FromString(Connection->GetAddress())));
}

void UTSScreenPlayer::OpenWindowSession()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	const FText Title = LOCTEXT("YourWindow", "Your shared window");
	if (!Host || !Host->EnsureConnected())
	{
		SetStatus(Title, Host ? Host->GetUnavailableReason() : LOCTEXT("NoHostWindow", "Telescreen's player isn't available."));
		return;
	}
	const uint64 Window = FCString::Strtoui64(*Screen->GetSourceUrl(), nullptr, 10);
	if (Window == 0)
	{
		SetStatus(Title, LOCTEXT("NoWindowChosen", "No window chosen."));
		return;
	}
	const bool bCursor = UTSConfig::GetBool(this, UTSConfig::WindowCursorKey, true);
	const bool bAudio = UTSConfig::GetBool(this, UTSConfig::WindowAudioKey, true);
	Session = Host->OpenWindow(Window, bCursor, bAudio, 1920, 1080, this);
	if (Session == 0)
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	bWindowSession = true;
	SessionKey = WantedKey;
	SetStatus(Title, LOCTEXT("WindowStarting", "Starting the capture..."));
}

void UTSScreenPlayer::BrowserMouseMove(const FVector2D& Position, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !AcceptsInput())
	{
		return;
	}
	const FIntPoint P = ToSourcePixels(Position);
	FTSPayloadWriter W;
	W.I32(P.X);
	W.I32(P.Y);
	W.U32(Modifiers);
	Host->SendBrowserInput(Session, EInput::MouseMove, W.Bytes);
}

void UTSScreenPlayer::BrowserMouseButton(const FVector2D& Position, uint8 Button, bool bUp, int32 ClickCount, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !AcceptsInput())
	{
		return;
	}
	const FIntPoint P = ToSourcePixels(Position);
	FTSPayloadWriter W;
	W.I32(P.X);
	W.I32(P.Y);
	W.U8(Button);
	W.U8(bUp ? 1 : 0);
	W.U8(uint8(FMath::Clamp(ClickCount, 1, 3)));
	W.U32(Modifiers);
	Host->SendBrowserInput(Session, EInput::MouseButton, W.Bytes);
}

void UTSScreenPlayer::BrowserWheel(const FVector2D& Position, int32 DeltaX, int32 DeltaY, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !AcceptsInput())
	{
		return;
	}
	const FIntPoint P = ToSourcePixels(Position);
	FTSPayloadWriter W;
	W.I32(P.X);
	W.I32(P.Y);
	W.I32(DeltaX);
	W.I32(DeltaY);
	W.U32(Modifiers);
	Host->SendBrowserInput(Session, EInput::Wheel, W.Bytes);
}

void UTSScreenPlayer::BrowserKey(EKeyType Type, int32 KeyCode, TCHAR Character, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !AcceptsInput())
	{
		return;
	}
	FTSPayloadWriter W;
	W.U8(uint8(Type));
	W.I32(KeyCode);
#if PLATFORM_WINDOWS
	W.I32(Type == EKeyType::Char ? 0 : int32(MapVirtualKeyW(UINT(KeyCode), MAPVK_VK_TO_VSC) << 16));
#else
	W.I32(0);
#endif
	W.U16(uint16(Character));
	W.U32(Modifiers);
	Host->SendBrowserInput(Session, EInput::Key, W.Bytes);
}

void UTSScreenPlayer::BrowserText(const FString& Text)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (Host && AcceptsInput() && !Text.IsEmpty())
	{
		FTSPayloadWriter W;
		W.Str(Text);
		Host->SendBrowserInput(Session, EInput::Text, W.Bytes);
	}
}

void UTSScreenPlayer::BrowserPointerDelta(int32 DeltaX, int32 DeltaY)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (Host && HasBrowser() && (DeltaX != 0 || DeltaY != 0))
	{
		FTSPayloadWriter W;
		W.I32(DeltaX);
		W.I32(DeltaY);
		Host->SendBrowserInput(Session, EInput::PointerDelta, W.Bytes);
	}
}

void UTSScreenPlayer::BrowserMouseLeave()
{
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && AcceptsInput())
	{
		Host->SendBrowserInput(Session, EInput::MouseLeave, TArray<uint8>());
	}
}

void UTSScreenPlayer::BrowserFocus(bool bFocused)
{
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && AcceptsInput())
	{
		FTSPayloadWriter W;
		W.U8(bFocused ? 1 : 0);
		Host->SendBrowserInput(Session, EInput::Focus, W.Bytes);
	}
}

void UTSScreenPlayer::BrowserNavigate(const FString& Url)
{
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && HasBrowser() && !Url.TrimStartAndEnd().IsEmpty())
	{
		FTSPayloadWriter W;
		W.Str(Url.TrimStartAndEnd());
		Host->SendBrowserCommand(Session, ECommand::Navigate, W.Bytes);
		UE_LOG(LogTelescreen, Log, TEXT("%s: browser opens %s"), *GetNameSafe(GetOwner()), *Url);
	}
}

void UTSScreenPlayer::BrowserCommand(ECommand Command)
{
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && HasBrowser())
	{
		Host->SendBrowserCommand(Session, Command, TArray<uint8>());
	}
}

UTSRemoteCallObject* UTSScreenPlayer::FindRemoteCallObject() const
{
	AFGPlayerController* Controller = Cast<AFGPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	return Controller ? Controller->GetRemoteCallObjectOfClass<UTSRemoteCallObject>() : nullptr;
}

void UTSScreenPlayer::UpdateStreamSender()
{
	ATSBuildableScreen* Screen = GetScreen();
	const bool bWant = Screen && (bBrowserSession || bWindowSession || bRemoteSession) && Session != 0 && Screen->GetMode() != ETSScreenMode::SyncedMedia && Screen->IsLocallyControlled() &&
		Screen->GetViewerCount() > 0 && !Screen->IsCurtainOn();
	if (SenderSession != 0 && (!bWant || SenderSource != Session))
	{
		CloseStreamSender();
	}
	if (bWant && SenderSession == 0)
	{
		OpenStreamSender();
	}
}

void UTSScreenPlayer::OpenStreamSender()
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || Session == 0)
	{
		return;
	}
	if (!SenderListener.IsValid())
	{
		SenderListener = MakeShared<FTSStreamSenderListener>(this);
	}
	const uint32 Kbps = uint32(FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::StreamKbpsKey, 2500), 300, 20000));
	const uint32 Fps = uint32(FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::StreamFpsKey, 30), 10, 60));
	SenderSession = Host->OpenStreamSender(Session, Fps, Kbps, SenderListener.Get());
	SenderSource = Session;
	CurrentKbps = WantedKbps = Kbps;
	LastCongestion = 0.0;
	NextBitrateChange = FPlatformTime::Seconds() + 5.0;
	ClearStreamQueue();
	NextPacketId[0] = NextPacketId[1] = 0;
	StreamStats = FText::GetEmpty();
	TSNetRate::Raise(GetWorld(), TSNetRate::StreamRate);
	UE_LOG(LogTelescreen, Log, TEXT("%s: streaming my source to %d viewer(s) at %u fps, %u kbps"), *GetNameSafe(GetOwner()), GetScreen() ? GetScreen()->GetViewerCount() : 0,
		Fps, Kbps);
}

void UTSScreenPlayer::CloseStreamSender()
{
	if (SenderSession == 0)
	{
		return;
	}
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
	{
		Host->CloseSession(SenderSession);
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: stopped streaming my source"), *GetNameSafe(GetOwner()));
	SenderSession = 0;
	SenderSource = 0;
	ClearStreamQueue();
	StreamStats = FText::GetEmpty();
}

void UTSScreenPlayer::OnSenderState(const FString& Message)
{
	StreamStats = FText::FromString(Message);
}

void UTSScreenPlayer::OnSenderLost()
{
	SenderSession = 0;
	SenderSource = 0;
}

void UTSScreenPlayer::OnKeyframeRequested()
{
	if (SenderSession != 0)
	{
		if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
		{
			Host->SendStreamControl(SenderSession, TSHostProtocol::EStreamControl::Keyframe, 0);
		}
	}
}

void UTSScreenPlayer::SendStreamChunks(uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, const TArray<uint8>& Data)
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen || SenderSession == 0 || Kind > 1)
	{
		return;
	}
	const double QueuedSeconds = OldestQueuedSeconds();
	if (QueuedSeconds > QueueDropSeconds)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("%s: stream send queue %.1f s behind; dropped it and asked for a keyframe"), *Screen->GetName(), QueuedSeconds);
		ClearStreamQueue();
		LastCongestion = FPlatformTime::Seconds();
		OnKeyframeRequested();
	}
	const int32 Count = FMath::Max(1, FMath::DivideAndRoundUp(Data.Num(), StreamChunkBytes));
	if (Count > MaxStreamChunks)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("%s: dropped a %d byte stream packet (too large)"), *Screen->GetName(), Data.Num());
		return;
	}
	const uint32 PacketId = NextPacketId[Kind]++;
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FQueuedChunk& Queued = SendQueue.AddDefaulted_GetRef();
		Queued.QueuedAt = Now;
		FTSStreamChunk& Chunk = Queued.Chunk;
		Chunk.Kind = Kind;
		Chunk.Flags = Flags;
		Chunk.PacketId = PacketId;
		Chunk.Index = uint16(Index);
		Chunk.Count = uint16(Count);
		Chunk.PtsMs = PtsMs;
		Chunk.Codec = Codec;
		const int32 Start = Index * StreamChunkBytes;
		Chunk.Data.Append(Data.GetData() + Start, FMath::Min(StreamChunkBytes, Data.Num() - Start));
		SendQueueBytes += Chunk.Data.Num();
	}
	FlushStreamQueue();
}

double UTSScreenPlayer::OldestQueuedSeconds() const
{
	return SendQueueHead < SendQueue.Num() ? FPlatformTime::Seconds() - SendQueue[SendQueueHead].QueuedAt : 0.0;
}

void UTSScreenPlayer::ClearStreamQueue()
{
	SendQueue.Reset();
	SendQueueHead = 0;
	SendQueueBytes = 0;
}

void UTSScreenPlayer::FlushStreamQueue()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = FindRemoteCallObject();
	if (!Screen || !RCO || SendQueueHead >= SendQueue.Num())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextRateRaise)
	{
		NextRateRaise = Now + 2.0;
		TSNetRate::Raise(GetWorld(), TSNetRate::StreamRate);
	}
	SendBudget = FMath::Min(StreamPaceBytesPerSecond * 0.1, SendBudget + (Now - SendBudgetAt) * StreamPaceBytesPerSecond);
	SendBudgetAt = Now;
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	UNetConnection* Connection = Controller ? Controller->GetNetConnection() : nullptr;
	while (SendQueueHead < SendQueue.Num())
	{
		const FTSStreamChunk& Chunk = SendQueue[SendQueueHead].Chunk;
		const int32 Cost = Chunk.Data.Num() + 32;
		if (SendBudget < Cost || (Connection && !Connection->IsNetReady()))
		{
			break;
		}
		RCO->Server_StreamChunk(Screen, Chunk);
		SendBudget -= Cost;
		SendQueueBytes -= Chunk.Data.Num();
		++SendQueueHead;
	}
	if (SendQueueHead >= SendQueue.Num())
	{
		ClearStreamQueue();
	}
	else if (SendQueueHead > 512)
	{
		SendQueue.RemoveAt(0, SendQueueHead);
		SendQueueHead = 0;
	}
}

void UTSScreenPlayer::AdaptBitrate()
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || SenderSession == 0 || CurrentKbps == 0)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double QueuedSeconds = OldestQueuedSeconds();
	if (QueuedSeconds > QueueLowerSeconds)
	{
		LastCongestion = Now;
	}
	if (Now < NextBitrateChange)
	{
		return;
	}
	uint32 NewKbps = CurrentKbps;
	if (Now - LastCongestion < 2.0)
	{
		NewKbps = FMath::Max<uint32>(300, CurrentKbps * 7 / 10);
	}
	else if (Now - LastCongestion > 10.0 && CurrentKbps < WantedKbps)
	{
		NewKbps = FMath::Min<uint32>(WantedKbps, CurrentKbps * 11 / 10 + 50);
	}
	if (NewKbps == CurrentKbps)
	{
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: stream bitrate %u -> %u kbps (%.2f s queued)"), *GetNameSafe(GetOwner()), CurrentKbps, NewKbps, QueuedSeconds);
	CurrentKbps = NewKbps;
	Host->SendStreamControl(SenderSession, TSHostProtocol::EStreamControl::Bitrate, NewKbps);
	NextBitrateChange = Now + 3.0;
}

void UTSScreenPlayer::ReconcileViewer(ATSBuildableScreen* Screen)
{
	const FText Title = GetControllerTitle();
	if (Screen->IsCurtainOn())
	{
		WantedKey.Reset();
		CloseSession();
		SetStatus(Title, LOCTEXT("StreamPaused", "They paused the stream."));
		return;
	}
	if (UTSConfig::GetInt(this, UTSConfig::ReceiveStreamsKey, 0) == 1)
	{
		WantedKey.Reset();
		CloseSession();
		SetStatus(Title, LOCTEXT("StreamsOff", "Watching other players' browsers and windows is turned off in your settings."));
		return;
	}
	if (!bActive)
	{
		WantedKey.Reset();
		CloseSession();
		SetStatus(Title, LOCTEXT("StreamFar", "Paused on this PC (far away, or too many screens playing)."));
		return;
	}
	const FString Key = TEXT("stream|") + Screen->GetControllerName();
	WantedKey = Key;
	if (Session != 0 && Key == SessionKey)
	{
		return;
	}
	CloseSession();
	OpenReceiverSession(Title);
}

void UTSScreenPlayer::OpenReceiverSession(const FText& Title)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !Host->EnsureConnected())
	{
		SetStatus(Title, Host ? Host->GetUnavailableReason() : LOCTEXT("NoHostViewer", "Telescreen's player isn't available."));
		return;
	}
	Session = Host->OpenStreamReceiver(this);
	if (Session == 0)
	{
		SetStatus(Title, Host->GetUnavailableReason());
		return;
	}
	bStreamReceiver = true;
	SessionKey = WantedKey;
	ResetReassembly();
	StreamStats = FText::GetEmpty();
	SetStatus(Title, LOCTEXT("StreamConnecting", "Connecting..."));
	SetWatching(true);
	RequestKeyframe(true);
}

void UTSScreenPlayer::SetWatching(bool bInWatching)
{
	if (bWatching == bInWatching)
	{
		return;
	}
	bWatching = bInWatching;
	if (UTSRemoteCallObject* RCO = FindRemoteCallObject())
	{
		RCO->Server_SetWatching(GetScreen(), bWatching);
	}
}

void UTSScreenPlayer::ResetReassembly()
{
	for (int32 K = 0; K < 2; ++K)
	{
		Partial[K] = FStreamPartial();
		bHaveLast[K] = false;
	}
	bAwaitingKeyframe = false;
}

void UTSScreenPlayer::RequestKeyframe(bool bForce)
{
	const bool bNewLoss = !bAwaitingKeyframe;
	if (bNewLoss)
	{
		if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && Session != 0)
		{
			Host->SendStreamControl(Session, TSHostProtocol::EStreamControl::Reset, 0);
		}
		bAwaitingKeyframe = true;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bForce && !bNewLoss && Now - LastKeyframeRequest < KeyframeRetrySeconds)
	{
		return;
	}
	LastKeyframeRequest = Now;
	UE_LOG(LogTelescreen, Verbose, TEXT("%s: asking for a stream keyframe"), *GetNameSafe(GetOwner()));
	if (UTSRemoteCallObject* RCO = FindRemoteCallObject())
	{
		RCO->Server_RequestKeyframe(GetScreen());
	}
}

void UTSScreenPlayer::OnStreamChunk(const FTSStreamChunk& Chunk)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!bStreamReceiver || Session == 0 || !Host || Chunk.Kind > 1 || Chunk.Count == 0 || Chunk.Index >= Chunk.Count)
	{
		return;
	}
	const int32 Kind = Chunk.Kind;
	FStreamPartial& P = Partial[Kind];
	if ((P.bActive && int32(Chunk.PacketId - P.PacketId) < 0) || (bHaveLast[Kind] && int32(Chunk.PacketId - LastPacketId[Kind]) <= 0))
	{
		return;
	}
	if (!P.bActive || Chunk.PacketId != P.PacketId)
	{
		const bool bLostPartial = P.bActive && P.Received < P.Parts.Num();
		const bool bGap = bHaveLast[Kind] && int32(Chunk.PacketId - LastPacketId[Kind]) > 1;
		if ((bLostPartial || bGap) && Kind == 0)
		{
			RequestKeyframe(false);
		}
		P = FStreamPartial();
		P.bActive = true;
		P.PacketId = Chunk.PacketId;
		P.Flags = Chunk.Flags;
		P.PtsMs = Chunk.PtsMs;
		P.Codec = Chunk.Codec;
		P.Parts.SetNum(Chunk.Count);
		P.Have.Init(false, Chunk.Count);
	}
	if (Chunk.Count != P.Parts.Num() || P.Have[Chunk.Index])
	{
		return;
	}
	P.Parts[Chunk.Index] = Chunk.Data;
	P.Have[Chunk.Index] = true;
	if (++P.Received < P.Parts.Num())
	{
		return;
	}
	TArray<uint8> Assembled;
	for (const TArray<uint8>& Part : P.Parts)
	{
		Assembled.Append(Part);
	}
	if (Kind == 0 && (P.Flags & 1))
	{
		bAwaitingKeyframe = false;
	}
	Host->SendStreamPacket(Session, uint8(Kind), P.Flags, P.PtsMs, P.Codec, Assembled);
	LastPacketId[Kind] = P.PacketId;
	bHaveLast[Kind] = true;
	P.bActive = false;
}

void UTSScreenPlayer::StartLoopback(UTSScreenPlayer* Source)
{
	if (!Source || Source == this || !Source->HasBrowser())
	{
		return;
	}
	CloseSession();
	LoopbackSource = Source;
	UE_LOG(LogTelescreen, Log, TEXT("%s: loopback test of %s"), *GetNameSafe(GetOwner()), *GetNameSafe(Source->GetOwner()));
	Reconcile();
}

void UTSScreenPlayer::StopLoopback()
{
	if (!LoopbackSource.IsValid() && SessionKey != TEXT("loopback"))
	{
		return;
	}
	LoopbackSource.Reset();
	CloseSession();
	Reconcile();
	ShowStatus();
}

void UTSScreenPlayer::OnHostBrowserState(const FTSBrowserState& State)
{
	if (State.bPointerLocked != BrowserState.bPointerLocked)
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: page %s the pointer"), *GetNameSafe(GetOwner()), State.bPointerLocked ? TEXT("locked") : TEXT("released"));
	}
	BrowserState = State;
	OnBrowserStateChanged.Broadcast();
}

void UTSScreenPlayer::CloseSession()
{
	CloseStreamSender();
	if (bStreamReceiver)
	{
		SetWatching(false);
		bStreamReceiver = false;
		ResetReassembly();
	}
	if (Session != 0)
	{
		if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
		{
			Host->CloseSession(Session);
		}
		Session = 0;
	}
	SessionKey.Reset();
	Video.Close();
	if (Wave)
	{
		Wave->SetReader(nullptr);
	}
	if (Audio)
	{
		Audio->Stop();
	}
	StopWwise();
	bShowingVideo = false;
	bBrowserSession = false;
	bWindowSession = false;
	bRemoteSession = false;
	ShownTexWidth = ShownWidth = 0;
}

void UTSScreenPlayer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (SessionKey == TEXT("loopback") && (!LoopbackSource.IsValid() || !LoopbackSource->HasBrowser()))
	{
		StopLoopback();
	}
	if (Now >= NextRank)
	{
		NextRank = Now + RankInterval;
		const bool bWasActive = bActive;
		RankActivity();
		if (bWasActive != bActive || (Session == 0 && !WantedKey.IsEmpty()))
		{
			Reconcile();
			if (!bShowingVideo)
			{
				ShowStatus();
			}
		}
	}
	if (Session == 0)
	{
		return;
	}
	PumpVideo();
	if (Now >= NextSenderCheck)
	{
		NextSenderCheck = Now + 0.5;
		UpdateStreamSender();
		AdaptBitrate();
	}
	if (SenderSession != 0)
	{
		FlushStreamQueue();
	}
	if (bStreamReceiver && bAwaitingKeyframe && Now - LastKeyframeRequest >= KeyframeRetrySeconds)
	{
		RequestKeyframe(false);
	}
	if (bBrowserSession)
	{
		const FIntPoint Wanted = ComputeBrowserSize();
		if (Now >= NextSync)
		{
			NextSync = Now + SyncInterval;
		}
		if (Wanted != BrowserSize)
		{
			BrowserSize = Wanted;
			if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
			{
				FTSPayloadWriter W;
				W.U32(uint32(Wanted.X));
				W.U32(uint32(Wanted.Y));
				Host->SendBrowserCommand(Session, ECommand::Resize, W.Bytes);
				UE_LOG(LogTelescreen, Log, TEXT("%s: browser resized to %dx%d"), *Screen->GetName(), Wanted.X, Wanted.Y);
			}
		}
	}
	else if (!bStreamReceiver && SessionKey != TEXT("loopback") && Now >= NextSync)
	{
		NextSync = Now + SyncInterval;
		if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this))
		{
			Host->SyncTarget(Session, Screen->GetPlaybackPosition(), Screen->GetMediaState().bPaused);
		}
	}
	if (Now >= NextAudioUpdate)
	{
		NextAudioUpdate = Now + AudioInterval;
		UpdateAudio();
	}
	// A HUD picture's sound follows the camera; a screen's emitter stays where it was placed when the sound started.
	if (WwisePlayingId != 0 && bPinned)
	{
		PlaceWwiseEmitter();
	}
}

void UTSScreenPlayer::RankActivity()
{
	ATSBuildableScreen* Screen = GetScreen();
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (!Screen || !Controller || !Controller->PlayerCameraManager)
	{
		bActive = false;
		return;
	}
	const FVector Viewer = Controller->PlayerCameraManager->GetCameraLocation();
	const double MaxDistance = UTSConfig::GetFloat(this, UTSConfig::PlayDistanceKey, 150.f) * 100.0;
	const int32 MaxActive = UTSConfig::GetInt(this, UTSConfig::MaxActiveScreensKey, 4);
	auto Wants = [&](const UTSScreenPlayer* Player, double& OutDistance)
	{
		const ATSBuildableScreen* S = Player->GetScreen();
		if (!S || (!S->GetMediaState().GetCurrent() && S->GetMode() == ETSScreenMode::SyncedMedia))
		{
			return false;
		}
		OutDistance = FVector::Dist(Viewer, S->GetActorLocation());
		return OutDistance <= MaxDistance;
	};
	if (Screen->GetMode() != ETSScreenMode::SyncedMedia && Screen->IsLocallyControlled())
	{
		bActive = true;
		return;
	}
	if (bPinned)
	{
		bActive = Screen->GetMode() != ETSScreenMode::SyncedMedia || Screen->GetMediaState().GetCurrent() != nullptr;
		return;
	}
	double MyDistance = 0.0;
	if (!Wants(this, MyDistance))
	{
		bActive = false;
		return;
	}
	int32 Closer = 0;
	for (const TWeakObjectPtr<UTSScreenPlayer>& Other : GPlayers)
	{
		double Distance = 0.0;
		if (Other.IsValid() && Other.Get() != this && !Other->bPinned && Other->GetWorld() == GetWorld() && Wants(Other.Get(), Distance) &&
			Distance < MyDistance)
		{
			++Closer;
		}
	}
	bActive = Closer < MaxActive;
}

void UTSScreenPlayer::PumpVideo()
{
	ATSBuildableScreen* Screen = GetScreen();
	double Pts = 0.0;
	if (!Screen || !Video.IsOpen() || !Video.FetchNewFrame(FrameBuffer, Pts))
	{
		return;
	}
	const int32 Width = int32(Video.GetWidth()), Height = int32(Video.GetHeight());
	const FVector2D Size = Screen->GetScreenWorldSize();
	const double Aspect = Size.Y > 1.0 ? Size.X / Size.Y : 2.0;
	// The browser renders at the screen's shape, so it (and its stream) fills the screen exactly.
	const bool bScreenShaped = bBrowserSession || (bStreamReceiver && Screen->GetMode() == ETSScreenMode::Browser);
	const FFit Fit = ComputeFit(Width, Height, Aspect, bScreenShaped ? ETSAspectMode::Stretch : Screen->GetAspectMode());
	ShownTexWidth = Fit.TexWidth;
	ShownTexHeight = Fit.TexHeight;
	ShownSrcX = Fit.SrcX;
	ShownSrcY = Fit.SrcY;
	ShownDstX = Fit.DstX;
	ShownDstY = Fit.DstY;
	ShownWidth = Width;
	ShownHeight = Height;
	const bool bMirrorX = UTSConfig::GetBool(this, UTSConfig::MirrorXKey, false);
	const bool bMirrorY = UTSConfig::GetBool(this, UTSConfig::MirrorYKey, false);

	if (!VideoTexture || VideoTexture->GetSizeX() != Fit.TexWidth || VideoTexture->GetSizeY() != Fit.TexHeight)
	{
		VideoTexture = UTexture2D::CreateTransient(Fit.TexWidth, Fit.TexHeight, PF_B8G8R8A8, TEXT("TelescreenVideo"));
		if (!VideoTexture)
		{
			UE_LOG(LogTelescreen, Error, TEXT("%s: could not create a %dx%d video texture"), *Screen->GetName(), Fit.TexWidth, Fit.TexHeight);
			return;
		}
		VideoTexture->SRGB = true;
		VideoTexture->Filter = TF_Bilinear;
		VideoTexture->NeverStream = true;
		VideoTexture->UpdateResource();
		bShowingVideo = false;
		UE_LOG(LogTelescreen, Log, TEXT("%s: video texture %dx%d for a %dx%d picture (aspect mode %d)"), *Screen->GetName(), Fit.TexWidth,
			Fit.TexHeight, Width, Height, int32(Screen->GetAspectMode()));
	}

	TArray<uint8>* Composed = new TArray<uint8>();
	Composed->SetNumZeroed(Fit.TexWidth * Fit.TexHeight * 4);
	const uint32* Src = reinterpret_cast<const uint32*>(FrameBuffer.GetData());
	uint32* Dst = reinterpret_cast<uint32*>(Composed->GetData());
	for (int32 Row = 0; Row < Fit.SrcHeight; ++Row)
	{
		const int32 OutRow = Fit.DstY + Row;
		uint32* DstRow = Dst + int64(bMirrorY ? Fit.TexHeight - 1 - OutRow : OutRow) * Fit.TexWidth;
		const uint32* SrcRow = Src + int64(Fit.SrcY + Row) * Width + Fit.SrcX;
		if (bMirrorX)
		{
			for (int32 X = 0; X < Fit.SrcWidth; ++X) DstRow[Fit.TexWidth - 1 - (Fit.DstX + X)] = SrcRow[X];
		}
		else
		{
			FMemory::Memcpy(DstRow + Fit.DstX, SrcRow, Fit.SrcWidth * 4);
		}
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Fit.TexWidth, Fit.TexHeight);
	VideoTexture->UpdateTextureRegions(0, 1, Region, Fit.TexWidth * 4, 4, Composed->GetData(),
		[Composed](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Composed;
			delete Regions;
		});
	if (!bShowingVideo)
	{
		bShowingVideo = true;
		Screen->SetScreenTexture(VideoTexture);
	}
}

void UTSScreenPlayer::SetPinned(bool bInPinned)
{
	if (bPinned == bInPinned)
	{
		return;
	}
	bPinned = bInPinned;
	ApplyAudioMode();
	PlaceWwiseEmitter();
	RankActivity();
	Reconcile();
	if (!bShowingVideo)
	{
		ShowStatus();
	}
}

UTexture* UTSScreenPlayer::GetDisplayTexture() const
{
	return bShowingVideo && VideoTexture ? static_cast<UTexture*>(VideoTexture) : static_cast<UTexture*>(StatusTarget);
}

void UTSScreenPlayer::ApplyAudioMode()
{
	if (!Audio)
	{
		return;
	}
	const bool b2D = bPinned && UTSConfig::GetBool(this, UTSConfig::PipAudio2DKey, true);
	Audio->bAllowSpatialization = !b2D;
	Audio->AttenuationOverrides.bAttenuate = !b2D;
	Audio->AttenuationOverrides.bSpatialize = !b2D;
}

void UTSScreenPlayer::UpdateAudio()
{
	const bool bMuted = UTSConfig::GetBool(this, UTSConfig::MuteInBackgroundKey, true) && !FPlatformApplicationMisc::IsThisApplicationForeground();
	const float Volume = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::VolumeKey, 1.f), 0.f, 1.f);
	if (WwisePlayingId != 0)
	{
		// The game's Master and Boom Box sliders apply on the Wwise bus.
		TSWwiseScreens::SetGain(WwisePlayingId, bMuted ? 0.f : Volume);
		if (TSWwiseScreens::HasEnded(WwisePlayingId))
		{
			// Wwise ended it (e.g. the game stopped every sound on the emitter): start again, or give up on Wwise for this
			// sound if it keeps ending at once.
			const bool bQuick = FPlatformTime::Seconds() - WwiseStartedAt < 1.0;
			WwiseQuickEnds = bQuick ? WwiseQuickEnds + 1 : 0;
			TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Reader = WwiseReader;
			UE_LOG(LogTelescreen, Log, TEXT("%s: Wwise ended the screen sound%s"), *GetNameSafe(GetOwner()), WwiseQuickEnds >= 2 ? TEXT("; using Unreal") : TEXT("; restarting"));
			StopWwise();
			if (Reader && WwiseQuickEnds < 2 && PlayThroughWwise(Reader))
			{
				return;
			}
			if (Reader && Wave == nullptr)
			{
				Wave = NewObject<UTSStreamingSoundWave>(this);
			}
			if (Reader && Wave)
			{
				Wave->SetReader(Reader);
				if (Audio)
				{
					Audio->SetSound(Wave);
					Audio->Play();
				}
			}
		}
		return;
	}
	if (!Audio)
	{
		return;
	}
	Audio->SetVolumeMultiplier(FMath::Max(bMuted ? 0.f : Volume * ReadMasterVolume(), 0.0001f));
}

bool UTSScreenPlayer::PlayThroughWwise(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& Reader)
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen || !UTSConfig::GetBool(this, UTSConfig::WwiseAudioKey, true) || !TSWwiseScreens::EnsureReady())
	{
		return false;
	}
	if (!WwiseEmitter)
	{
		WwiseEmitter = TSWwiseScreens::CreateEmitter(Screen->GetRootComponent());
		if (!WwiseEmitter)
		{
			return false;
		}
	}
	// Bigger screens carry further: the Boom Box falloff the bank came with, scaled by the screen's width (8 m = 1).
	const float Range = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::AudioRangeKey, 1.f), 0.1f, 10.f);
	const float Scale = FMath::Max(0.25f, float(Screen->GetScreenWorldSize().X) / 800.f) * Range;
	TSWwiseScreens::SetAttenuationScale(WwiseEmitter, Scale);
	StopWwise();
	PlaceWwiseEmitter();
	WwisePlayingId = TSWwiseScreens::Play(Reader, WwiseEmitter);
	if (WwisePlayingId == 0)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("%s: Wwise wouldn't play the screen sound; using Unreal"), *Screen->GetName());
		return false;
	}
	WwiseReader = Reader;
	WwiseStartedAt = FPlatformTime::Seconds();
	UpdateAudio();
	UE_LOG(LogTelescreen, Log, TEXT("%s: sound plays through Wwise (playing id %u, attenuation x%.2f)"), *Screen->GetName(), WwisePlayingId, Scale);
	return true;
}

void UTSScreenPlayer::StopWwise()
{
	if (WwisePlayingId != 0)
	{
		TSWwiseScreens::Stop(WwisePlayingId);
		WwisePlayingId = 0;
	}
	WwiseReader.Reset();
}

void UTSScreenPlayer::PlaceWwiseEmitter()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!WwiseEmitter || !Screen)
	{
		return;
	}
	// A HUD picture with "hear it everywhere": the sound follows the camera, so it is heard at full volume.
	FVector Where = Screen->GetScreenCenter();
	if (bPinned && UTSConfig::GetBool(this, UTSConfig::PipAudio2DKey, true))
	{
		if (const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0); Controller && Controller->PlayerCameraManager)
		{
			Where = Controller->PlayerCameraManager->GetCameraLocation();
		}
	}
	WwiseEmitter->SetWorldLocation(Where);
	TSWwiseScreens::UpdatePosition(WwiseEmitter);
}

void UTSScreenPlayer::OnHostVideo(uint32 Generation, uint32 Width, uint32 Height, const FString& MappingName)
{
	if (!Video.Open(MappingName))
	{
		UE_LOG(LogTelescreen, Error, TEXT("Could not open video %s"), *MappingName);
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: video generation %u, %ux%u"), *GetNameSafe(GetOwner()), Generation, Width, Height);
}

void UTSScreenPlayer::OnHostAudio(uint32 SampleRate, uint32 Channels, uint32 CapacityFrames, const FString& MappingName)
{
	ATSBuildableScreen* Screen = GetScreen();
	TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Reader = FTSAudioReader::Open(MappingName);
	if (!Screen || !Reader.IsValid())
	{
		UE_LOG(LogTelescreen, Error, TEXT("Could not open audio %s"), *MappingName);
		return;
	}
	if (PlayThroughWwise(Reader))
	{
		if (Audio)
		{
			Audio->Stop();
		}
		if (Wave)
		{
			Wave->SetReader(nullptr);
		}
		return;
	}
	if (!Wave)
	{
		Wave = NewObject<UTSStreamingSoundWave>(this);
	}
	Wave->SetReader(Reader);
	if (!Audio)
	{
		Audio = NewObject<UAudioComponent>(Screen, TEXT("TelescreenAudio"));
		Audio->bAutoActivate = false;
		Audio->bAllowSpatialization = true;
		Audio->bOverrideAttenuation = true;
		Audio->RegisterComponent();
	}

	// Bigger screens carry further: full volume within half the screen's width, fading out over ten widths.
	const FVector2D Size = Screen->GetScreenWorldSize();
	const float Range = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::AudioRangeKey, 1.f), 0.1f, 10.f);
	Audio->AttenuationOverrides.bAttenuate = true;
	Audio->AttenuationOverrides.bSpatialize = true;
	Audio->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(float(Size.X) * 0.5f * Range, 0.f, 0.f);
	Audio->AttenuationOverrides.FalloffDistance = float(Size.X) * 10.f * Range;
	Audio->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;
	Audio->SetWorldLocation(Screen->GetScreenCenter());
	Audio->SetSound(Wave);
	ApplyAudioMode();
	UpdateAudio();
	Audio->Play();
	UE_LOG(LogTelescreen, Log, TEXT("%s: audio %u Hz x%u, heard up to %.0f m"), *Screen->GetName(), SampleRate, Channels,
		(Size.X * 0.5f + Size.X * 10.f) * Range / 100.f);
}

void UTSScreenPlayer::OnHostState(ESessionState State, double Position, double Duration, const FString& Message)
{
	ATSBuildableScreen* Screen = GetScreen();
	const FTSMediaItem* Item = Screen ? Screen->GetMediaState().GetCurrent() : nullptr;
	FText Title = Item ? FText::FromString(Item->GetDisplayTitle()) : FText::GetEmpty();
	if (bStreamReceiver && Screen)
	{
		Title = GetControllerTitle();
	}
	else if (bWindowSession)
	{
		Title = LOCTEXT("YourWindow", "Your shared window");
	}
	else if (bRemoteSession)
	{
		Title = LOCTEXT("YourRemote", "Your remote desktop");
	}
	if (State != HostState)
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: session %u state %d at %.1f / %.0f s %s"), *GetNameSafe(GetOwner()), Session, int32(State), Position, Duration,
			*Message);
	}
	HostState = State;
	if (LoopbackSource.IsValid() && State == ESessionState::Playing)
	{
		LoopbackStats = FText::FromString(Message);
	}
	else if (bStreamReceiver && State == ESessionState::Playing && !Message.IsEmpty())
	{
		StreamStats = FText::FromString(Message);
	}
	if ((bWindowSession || bRemoteSession) && !Message.IsEmpty() && (State == ESessionState::Error || State == ESessionState::Buffering || State == ESessionState::Resolving))
	{
		SetStatus(Title, FText::FromString(Message));
		// Minimised: the controller sees why the picture stopped (viewers keep the last frame) until frames return.
		bShowingVideo = false;
		ShowStatus();
		if (State == ESessionState::Error)
		{
			// The window closed or can't be captured, or the remote desktop disconnected: give the screen back.
			bShowingVideo = false;
			ShowStatus();
			UTSRemoteCallObject::OnNotice.Broadcast(FText::FromString(Message));
			if (UTSRemoteCallObject* RCO = FindRemoteCallObject(); RCO && Screen)
			{
				RCO->Server_ReleaseControl(Screen);
			}
		}
		return;
	}
	switch (State)
	{
	case ESessionState::Error:
		bShowingVideo = false;
		SetStatus(Title, FText::Format(LOCTEXT("PlayError", "Could not play: {0}"), FText::FromString(Message)));
		ShowStatus();
		break;
	case ESessionState::Ended:
		bShowingVideo = false;
		SetStatus(Title, LOCTEXT("Ended", "Finished"));
		ShowStatus();
		break;
	case ESessionState::Resolving:
		SetStatus(Title, LOCTEXT("Resolving", "Loading..."));
		break;
	case ESessionState::Buffering:
		SetStatus(Title, LOCTEXT("Buffering", "Buffering..."));
		break;
	default:
		break;
	}
	if (!bShowingVideo)
	{
		ShowStatus();
	}
}

void UTSScreenPlayer::OnHostLost()
{
	UE_LOG(LogTelescreen, Warning, TEXT("%s: player stopped; reopening"), *GetNameSafe(GetOwner()));
	Session = 0;
	SessionKey.Reset();
	Video.Close();
	if (Wave)
	{
		Wave->SetReader(nullptr);
	}
	StopWwise();
	bShowingVideo = false;
	SetStatus(StatusLine1, LOCTEXT("HostLost", "Player restarting..."));
	ShowStatus();
}

void UTSScreenPlayer::SetStatus(const FText& Line1, const FText& Line2)
{
	const bool bChanged = !Line1.EqualTo(StatusLine1) || !Line2.EqualTo(StatusLine2);
	StatusLine1 = Line1;
	StatusLine2 = Line2;
	if (bChanged && !bShowingVideo)
	{
		ShowStatus();
	}
}

void UTSScreenPlayer::ShowStatus()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen || !StatusTarget)
	{
		return;
	}
	StatusTarget->UpdateResource();
	Screen->SetScreenTexture(StatusTarget);
}

void UTSScreenPlayer::DrawStatus(UCanvas* Canvas, int32 Width, int32 Height)
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Canvas || !Font)
	{
		return;
	}
	auto DrawCentered = [&](const FString& Text, float CenterY, float Scale, const FLinearColor& Color)
	{
		float TextWidth = 0.f, TextHeight = 0.f;
		Canvas->TextSize(Font, Text, TextWidth, TextHeight, Scale, Scale);
		if (TextWidth > Width * 0.92f && TextWidth > 0.f)
		{
			Scale *= Width * 0.92f / TextWidth;
			Canvas->TextSize(Font, Text, TextWidth, TextHeight, Scale, Scale);
		}
		Canvas->SetDrawColor(Color.ToFColor(true));
		Canvas->DrawText(Font, Text, (Width - TextWidth) * 0.5f, CenterY - TextHeight * 0.5f, Scale, Scale);
	};
	const float Base = Height / 256.f;
	DrawCentered(TEXT("TELESCREEN"), Height * 0.18f, 0.8f * Base, FLinearColor(0.95f, 0.55f, 0.1f));
	DrawCentered(StatusLine1.ToString(), Height * 0.45f, 1.6f * Base, FLinearColor::White);
	DrawCentered(StatusLine2.ToString(), Height * 0.68f, 1.0f * Base, FLinearColor(0.7f, 0.75f, 0.8f));
}

#undef LOCTEXT_NAMESPACE
