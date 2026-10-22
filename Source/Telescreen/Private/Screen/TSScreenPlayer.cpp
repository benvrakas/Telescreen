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
#include "Telescreen.h"
#include "TSConfig.h"
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
	}
	if (Audio)
	{
		Audio->Stop();
		Audio->DestroyComponent();
		Audio = nullptr;
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
	if (Screen->GetMode() == ETSScreenMode::Browser)
	{
		BlockedReason = FText::GetEmpty();
		if (!Screen->IsLocallyControlled())
		{
			WantedKey.Reset();
			CloseSession();
			SetStatus(FText::Format(LOCTEXT("OtherBrowser", "{0} is using the browser"), FText::FromString(Screen->GetControllerName())),
				LOCTEXT("OtherBrowserLater", "Everyone will see it here once live streaming arrives in a later update."));
			return;
		}
		const FString Key = TEXT("browser|") + Screen->GetSourceUrl();
		WantedKey = Key;
		if (Session != 0 && Key == SessionKey)
		{
			return;
		}
		CloseSession();
		OpenBrowserSession();
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

FIntPoint UTSScreenPlayer::ToBrowserPixels(const FVector2D& Position) const
{
	return FIntPoint(FMath::Clamp(int32(Position.X * BrowserSize.X), 0, BrowserSize.X - 1), FMath::Clamp(int32(Position.Y * BrowserSize.Y), 0, BrowserSize.Y - 1));
}

void UTSScreenPlayer::BrowserMouseMove(const FVector2D& Position, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !HasBrowser())
	{
		return;
	}
	const FIntPoint P = ToBrowserPixels(Position);
	FTSPayloadWriter W;
	W.I32(P.X);
	W.I32(P.Y);
	W.U32(Modifiers);
	Host->SendBrowserInput(Session, EInput::MouseMove, W.Bytes);
}

void UTSScreenPlayer::BrowserMouseButton(const FVector2D& Position, uint8 Button, bool bUp, int32 ClickCount, uint32 Modifiers)
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !HasBrowser())
	{
		return;
	}
	const FIntPoint P = ToBrowserPixels(Position);
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
	if (!Host || !HasBrowser())
	{
		return;
	}
	const FIntPoint P = ToBrowserPixels(Position);
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
	if (!Host || !HasBrowser())
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
	if (Host && HasBrowser() && !Text.IsEmpty())
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
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && HasBrowser())
	{
		Host->SendBrowserInput(Session, EInput::MouseLeave, TArray<uint8>());
	}
}

void UTSScreenPlayer::BrowserFocus(bool bFocused)
{
	if (UTSHostSubsystem* Host = UTSHostSubsystem::Get(this); Host && HasBrowser())
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
	bShowingVideo = false;
	bBrowserSession = false;
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
	else if (Now >= NextSync)
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
		if (!S || !S->GetMediaState().GetCurrent())
		{
			return false;
		}
		OutDistance = FVector::Dist(Viewer, S->GetActorLocation());
		return OutDistance <= MaxDistance;
	};
	if (Screen->GetMode() == ETSScreenMode::Browser)
	{
		bActive = Screen->IsLocallyControlled();
		return;
	}
	if (bPinned)
	{
		bActive = Screen->GetMediaState().GetCurrent() != nullptr;
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
	const FFit Fit = ComputeFit(Width, Height, Aspect, bBrowserSession ? ETSAspectMode::Stretch : Screen->GetAspectMode());
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
	if (!Audio)
	{
		return;
	}
	float Volume = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::VolumeKey, 1.f), 0.f, 1.f) * ReadMasterVolume();
	if (UTSConfig::GetBool(this, UTSConfig::MuteInBackgroundKey, true) && !FPlatformApplicationMisc::IsThisApplicationForeground())
	{
		Volume = 0.f;
	}
	Audio->SetVolumeMultiplier(FMath::Max(Volume, 0.0001f));
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
	Audio->SetWorldLocation(Screen->GetActorLocation());
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
	const FText Title = Item ? FText::FromString(Item->GetDisplayTitle()) : FText::GetEmpty();
	if (State != HostState)
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: session %u state %d at %.1f / %.0f s %s"), *GetNameSafe(GetOwner()), Session, int32(State), Position, Duration,
			*Message);
	}
	HostState = State;
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
