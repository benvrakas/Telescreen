#include "TSRemoteCallObject.h"
#include "EngineUtils.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Telescreen.h"
#include "Net/TSNetRate.h"
#include "TSConfig.h"

FTSOnNotice UTSRemoteCallObject::OnNotice;

namespace
{
	constexpr int32 MaxChunkBytes = 900;
	constexpr int32 MaxChunksPerPacket = 1024;
	constexpr int32 MaxUrlLength = 2048;
	constexpr int32 MaxTitleLength = 300;
	constexpr int32 MaxNameLength = 64;
}

void UTSRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTSRemoteCallObject, mForceNetField_UTSRemoteCallObject);
}

UTSRemoteCallObject* UTSRemoteCallObject::GetLocal(const UObject* WorldContext)
{
	AFGPlayerController* Controller = WorldContext ? Cast<AFGPlayerController>(UGameplayStatics::GetPlayerController(WorldContext, 0)) : nullptr;
	UTSRemoteCallObject* RCO = Controller ? Controller->GetRemoteCallObjectOfClass<UTSRemoteCallObject>() : nullptr;
	if (!RCO)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Remote call object not available (is Telescreen installed on the server?)"));
	}
	return RCO;
}

bool UTSRemoteCallObject::IsHostOrAdmin(const APlayerController* Controller)
{
	if (!Controller)
	{
		return false;
	}
	if (Controller->GetNetMode() != NM_DedicatedServer && Controller->GetNetMode() != NM_Client && Controller->IsLocalController())
	{
		return true;
	}
	const AFGPlayerState* State = Cast<AFGPlayerState>(Controller->PlayerState);
	return State && State->IsServerAdmin();
}

bool UTSRemoteCallObject::IsLocalHostOrAdmin(const UObject* WorldContext)
{
	const APlayerController* Controller = WorldContext ? UGameplayStatics::GetPlayerController(WorldContext, 0) : nullptr;
	if (!Controller)
	{
		return false;
	}
	if (Controller->GetNetMode() == NM_Standalone || Controller->GetNetMode() == NM_ListenServer)
	{
		return true;
	}
	const AFGPlayerState* State = Cast<AFGPlayerState>(Controller->PlayerState);
	return State && State->IsServerAdmin();
}

FString UTSRemoteCallObject::GetRequesterName() const
{
	const APlayerController* Controller = GetOwnerPlayerController();
	return Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : TEXT("someone");
}

bool UTSRemoteCallObject::MayEdit(const ATSBuildableScreen* Screen)
{
	if (!Screen)
	{
		return false;
	}
	if (ETSQueuePolicy(UTSConfig::GetInt(Screen, UTSConfig::QueuePolicyKey, 0)) != ETSQueuePolicy::HostOnly)
	{
		return true;
	}
	if (IsHostOrAdmin(GetOwnerPlayerController()))
	{
		return true;
	}
	Client_Notice(TEXT("Only the world host can change screen videos on this world."));
	return false;
}

bool UTSRemoteCallObject::MayUseMode(const ATSBuildableScreen* Screen, ETSScreenMode Mode)
{
	const APlayerController* Controller = GetOwnerPlayerController();
	if (!Screen || !Controller)
	{
		return false;
	}
	const bool bHost = IsHostOrAdmin(Controller);
	const TCHAR* What = Mode == ETSScreenMode::Window ? TEXT("share a window") : Mode == ETSScreenMode::RemoteDesktop ? TEXT("show a remote desktop") : TEXT("use the browser");
	if (Mode == ETSScreenMode::Browser && !UTSConfig::GetBool(Screen, UTSConfig::AllowBrowserKey, true))
	{
		Client_Notice(TEXT("The built-in browser is turned off on this world."));
		return false;
	}
	const FString& RoleKey = Mode == ETSScreenMode::Window ? UTSConfig::WindowRoleKey : Mode == ETSScreenMode::RemoteDesktop ? UTSConfig::RemoteDesktopRoleKey : UTSConfig::BrowserRoleKey;
	const int32 DefaultRole = Mode == ETSScreenMode::Window ? int32(ETSRole::Trusted) : Mode == ETSScreenMode::RemoteDesktop ? int32(ETSRole::HostAndAdmins) : int32(ETSRole::Everyone);
	const ETSRole Role = ETSRole(FMath::Clamp(UTSConfig::GetInt(Screen, RoleKey, DefaultRole), 0, 2));
	const bool bTrusted = bHost || UTSConfig::IsTrustedPlayer(Screen, GetRequesterName());
	if ((Role == ETSRole::Trusted && !bTrusted) || (Role == ETSRole::HostAndAdmins && !bHost))
	{
		Client_Notice(FString::Printf(TEXT("Only %s can %s on this world."), Role == ETSRole::Trusted ? TEXT("trusted players") : TEXT("the world host"), What));
		return false;
	}
	const bool bSomeoneElse = Screen->GetMode() != ETSScreenMode::SyncedMedia && Screen->GetController() && Screen->GetController() != Controller->PlayerState;
	if (bSomeoneElse && !bHost && !UTSConfig::GetBool(Screen, UTSConfig::AllowTakeOverKey, true))
	{
		Client_Notice(FString::Printf(TEXT("%s is using this screen; wait until they stop."), *Screen->GetControllerName()));
		return false;
	}
	if (Screen->GetMode() == ETSScreenMode::SyncedMedia)
	{
		int32 InUse = 0;
		for (TActorIterator<ATSBuildableScreen> It(Screen->GetWorld()); It; ++It)
		{
			if (It->GetMode() != ETSScreenMode::SyncedMedia && It->GetController() != Controller->PlayerState)
			{
				++InUse;
			}
		}
		const int32 Limit = FMath::Max(1, UTSConfig::GetInt(Screen, UTSConfig::MaxControlledScreensKey, 4));
		if (InUse >= Limit && !bHost)
		{
			Client_Notice(FString::Printf(TEXT("%d screens are already in use on this world; wait until one is free."), InUse));
			return false;
		}
	}
	return true;
}

// Validation only rejects messages the mod never sends; a failed check disconnects the player, so ordinary mistakes
// (too long a link, a long title) are refused in the implementation with a notice instead.
bool UTSRemoteCallObject::Server_Enqueue_Validate(ATSBuildableScreen* Screen, const FTSMediaItem& Item, bool bPlayNow)
{
	return FMath::IsFinite(Item.Duration);
}

void UTSRemoteCallObject::Server_Enqueue_Implementation(ATSBuildableScreen* Screen, const FTSMediaItem& Item, bool bPlayNow)
{
	const FString Requester = GetRequesterName();
	UE_LOG(LogTelescreen, Log, TEXT("Server_Enqueue from %s on %s: %s"), *Requester, *GetNameSafe(Screen), *Item.Url.Left(300));
	if (!MayEdit(Screen))
	{
		return;
	}
	if (Item.Url.Len() > MaxUrlLength)
	{
		Client_Notice(TEXT("That link is too long."));
		return;
	}
	const FString Url = Item.Url.TrimStartAndEnd();
	if (Url.IsEmpty())
	{
		return;
	}
	const bool bWebLink = Url.StartsWith(TEXT("http://")) || Url.StartsWith(TEXT("https://"));
	if (Url.Contains(TEXT("://")) && !bWebLink)
	{
		Client_Notice(TEXT("Only http(s) links and file paths can be played."));
		return;
	}
	if (bWebLink && !UTSConfig::GetBool(Screen, UTSConfig::AllowAnySiteKey, false) && !UTSConfig::IsAllowedSite(Screen, Url))
	{
		Client_Notice(TEXT("This world only allows videos from its allowed sites."));
		return;
	}
	const int32 MaxMinutes = UTSConfig::GetInt(Screen, UTSConfig::MaxVideoMinutesKey, 0);
	if (MaxMinutes > 0 && Item.Duration > MaxMinutes * 60.f)
	{
		Client_Notice(FString::Printf(TEXT("Videos longer than %d minutes aren't allowed on this world."), MaxMinutes));
		return;
	}
	const double Now = FPlatformTime::Seconds();
	RecentAdds.RemoveAll([Now](double T) { return Now - T > 60.0; });
	if (RecentAdds.Num() >= FMath::Max(1, UTSConfig::GetInt(Screen, UTSConfig::QueueAddsPerMinuteKey, 10)))
	{
		Client_Notice(TEXT("You're adding videos too quickly; wait a moment."));
		return;
	}
	RecentAdds.Add(Now);

	FTSMediaItem Accepted = Item;
	Accepted.Url = Url;
	Accepted.Title = Item.Title.Left(MaxTitleLength);
	Accepted.AddedBy = Requester;
	Accepted.Duration = FMath::Max(0.f, Accepted.Duration);
	Screen->Server_Enqueue(Accepted, bPlayNow);
}

bool UTSRemoteCallObject::Server_MediaCommand_Validate(ATSBuildableScreen* Screen, ETSMediaCommand Command, int32 Index, double Seconds)
{
	return uint8(Command) <= uint8(ETSMediaCommand::Clear) && FMath::IsFinite(Seconds);
}

void UTSRemoteCallObject::Server_MediaCommand_Implementation(ATSBuildableScreen* Screen, ETSMediaCommand Command, int32 Index, double Seconds)
{
	UE_LOG(LogTelescreen, Log, TEXT("Server_MediaCommand from %s on %s: %d (%d, %.1f)"), *GetRequesterName(), *GetNameSafe(Screen), int32(Command),
		Index, Seconds);
	if (MayEdit(Screen))
	{
		Screen->ApplyMediaCommand(Command, Index, Seconds);
	}
}

bool UTSRemoteCallObject::Server_SetScreenSettings_Validate(ATSBuildableScreen* Screen, const FString& Name, ETSAspectMode AspectMode)
{
	return uint8(AspectMode) <= uint8(ETSAspectMode::Stretch);
}

void UTSRemoteCallObject::Server_SetScreenSettings_Implementation(ATSBuildableScreen* Screen, const FString& Name, ETSAspectMode AspectMode)
{
	UE_LOG(LogTelescreen, Log, TEXT("Server_SetScreenSettings from %s on %s: \"%s\", aspect %d"), *GetRequesterName(), *GetNameSafe(Screen), *Name.Left(MaxNameLength),
		int32(AspectMode));
	if (MayEdit(Screen))
	{
		Screen->Server_SetScreenSettings(Name.Left(MaxNameLength), AspectMode);
	}
}

bool UTSRemoteCallObject::Server_TakeControl_Validate(ATSBuildableScreen* Screen, ETSScreenMode Mode, const FString& Url)
{
	return uint8(Mode) <= uint8(ETSScreenMode::RemoteDesktop);
}

void UTSRemoteCallObject::Server_TakeControl_Implementation(ATSBuildableScreen* Screen, ETSScreenMode Mode, const FString& Url)
{
	APlayerController* Controller = GetOwnerPlayerController();
	UE_LOG(LogTelescreen, Log, TEXT("Server_TakeControl from %s on %s: mode %d, %s"), *GetRequesterName(), *GetNameSafe(Screen), int32(Mode), *Url.Left(300));
	if (!Screen || !Controller || !Controller->PlayerState || Mode == ETSScreenMode::SyncedMedia)
	{
		return;
	}
	if (Url.Len() > MaxUrlLength)
	{
		Client_Notice(TEXT("That address is too long."));
		return;
	}
	if (!MayUseMode(Screen, Mode))
	{
		return;
	}
	// One screen per player: their other screen goes back to its videos.
	for (TActorIterator<ATSBuildableScreen> It(Screen->GetWorld()); It; ++It)
	{
		if (*It != Screen && It->GetMode() != ETSScreenMode::SyncedMedia && It->GetController() == Controller->PlayerState)
		{
			It->Server_ReleaseControl(TEXT("controller moved to another screen"));
		}
	}
	Screen->Server_TakeControl(Controller->PlayerState, Mode, Url.TrimStartAndEnd());
}

bool UTSRemoteCallObject::Server_ReleaseControl_Validate(ATSBuildableScreen* Screen)
{
	return true;
}

void UTSRemoteCallObject::Server_ReleaseControl_Implementation(ATSBuildableScreen* Screen)
{
	const APlayerController* Controller = GetOwnerPlayerController();
	if (!Screen || !Controller)
	{
		return;
	}
	const bool bIsController = Screen->GetController() == Controller->PlayerState;
	const bool bIsHost = IsHostOrAdmin(Controller);
	if (bIsController || bIsHost)
	{
		Screen->Server_ReleaseControl(bIsController ? TEXT("controller stopped") : *FString::Printf(TEXT("stopped by %s"), *GetRequesterName()));
	}
}

bool UTSRemoteCallObject::Server_StreamChunk_Validate(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk)
{
	return Chunk.Data.Num() <= MaxChunkBytes && Chunk.Count >= 1 && Chunk.Count <= MaxChunksPerPacket && Chunk.Index < Chunk.Count && Chunk.Kind <= 1;
}

void UTSRemoteCallObject::Server_StreamChunk_Implementation(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk)
{
	const APlayerController* Sender = GetOwnerPlayerController();
	if (!Screen || !Sender || Screen->GetMode() == ETSScreenMode::SyncedMedia || Screen->GetController() != Sender->PlayerState || Screen->IsCurtainOn() ||
		Screen->GetWatchers().Num() == 0 || !UTSConfig::GetBool(Screen, UTSConfig::AllowStreamingKey, true))
	{
		return;
	}
	if (!Screen->Server_AllowStreamBytes(Chunk.Data.Num() + 24))
	{
		return;
	}
	TSNetRate::Raise(Screen->GetWorld(), TSNetRate::StreamRate);
	for (const TWeakObjectPtr<APlayerController>& Watcher : Screen->GetWatchers())
	{
		if (!Watcher.IsValid())
		{
			continue;
		}
		if (Watcher->IsLocalController())
		{
			Screen->OnStreamChunk.Broadcast(Chunk);
		}
		else if (AFGPlayerController* Remote = Cast<AFGPlayerController>(Watcher.Get()))
		{
			if (UTSRemoteCallObject* RCO = Remote->GetRemoteCallObjectOfClass<UTSRemoteCallObject>())
			{
				RCO->Client_StreamChunk(Screen, Chunk);
			}
		}
	}
}

void UTSRemoteCallObject::Client_StreamChunk_Implementation(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk)
{
	if (Screen)
	{
		Screen->OnStreamChunk.Broadcast(Chunk);
	}
}

bool UTSRemoteCallObject::Server_SetWatching_Validate(ATSBuildableScreen* Screen, bool bWatching)
{
	return true;
}

void UTSRemoteCallObject::Server_SetWatching_Implementation(ATSBuildableScreen* Screen, bool bWatching)
{
	APlayerController* Viewer = GetOwnerPlayerController();
	if (!Screen || !Viewer)
	{
		return;
	}
	if (bWatching && !UTSConfig::GetBool(Screen, UTSConfig::AllowStreamingKey, true))
	{
		Client_Notice(TEXT("Sharing browsers is turned off on this world."));
		return;
	}
	if (bWatching)
	{
		TSNetRate::Raise(Screen->GetWorld(), TSNetRate::StreamRate);
	}
	Screen->Server_SetWatcher(Viewer, bWatching);
}

bool UTSRemoteCallObject::Server_SetCurtain_Validate(ATSBuildableScreen* Screen, bool bOn)
{
	return true;
}

void UTSRemoteCallObject::Server_SetCurtain_Implementation(ATSBuildableScreen* Screen, bool bOn)
{
	const APlayerController* Controller = GetOwnerPlayerController();
	if (Screen && Controller && Screen->GetController() == Controller->PlayerState)
	{
		Screen->Server_SetCurtain(bOn);
	}
}

bool UTSRemoteCallObject::Server_RequestKeyframe_Validate(ATSBuildableScreen* Screen)
{
	return true;
}

void UTSRemoteCallObject::Server_RequestKeyframe_Implementation(ATSBuildableScreen* Screen)
{
	const APlayerController* Viewer = GetOwnerPlayerController();
	if (Screen && Viewer && Screen->GetWatchers().Contains(Viewer) && Screen->Server_AllowKeyframeRequest())
	{
		SendKeyframeRequest(Screen);
	}
}

void UTSRemoteCallObject::Client_RequestKeyframe_Implementation(ATSBuildableScreen* Screen)
{
	if (Screen)
	{
		Screen->OnKeyframeRequested.Broadcast();
	}
}

bool UTSRemoteCallObject::Server_SetInputGrant_Validate(ATSBuildableScreen* Screen, APlayerState* Viewer, ETSInputScope Scope, int32 Minutes)
{
	return uint8(Scope) <= uint8(ETSInputScope::Full);
}

void UTSRemoteCallObject::Server_SetInputGrant_Implementation(ATSBuildableScreen* Screen, APlayerState* Viewer, ETSInputScope Scope, int32 Minutes)
{
	const APlayerController* Controller = GetOwnerPlayerController();
	if (!Screen || !Controller || Screen->GetController() != Controller->PlayerState)
	{
		return;
	}
	if (Scope != ETSInputScope::None && Screen->GetMaxGrantScope() == ETSInputScope::None)
	{
		Client_Notice(TEXT("The world host doesn't let viewers control this kind of screen."));
		return;
	}
	Screen->Server_SetGrant(Viewer, Scope, Minutes);
}

bool UTSRemoteCallObject::Server_ViewerPointer_Validate(ATSBuildableScreen* Screen, const FTSViewerInput& Input)
{
	return Input.Text.Len() <= 256;
}

void UTSRemoteCallObject::Server_ViewerPointer_Implementation(ATSBuildableScreen* Screen, const FTSViewerInput& Input)
{
	RelayViewerInput(Screen, Input, false);
}

bool UTSRemoteCallObject::Server_ViewerInput_Validate(ATSBuildableScreen* Screen, const FTSViewerInput& Input)
{
	return Input.Text.Len() <= 256;
}

void UTSRemoteCallObject::Server_ViewerInput_Implementation(ATSBuildableScreen* Screen, const FTSViewerInput& Input)
{
	RelayViewerInput(Screen, Input, true);
}

void UTSRemoteCallObject::RelayViewerInput(ATSBuildableScreen* Screen, const FTSViewerInput& Input, bool bReliable)
{
	const APlayerController* Viewer = GetOwnerPlayerController();
	if (!Screen || !Viewer || !Viewer->PlayerState || Screen->IsCurtainOn())
	{
		return;
	}
	const ETSInputScope Scope = Screen->GetGrantFor(Viewer->PlayerState);
	if (Scope == ETSInputScope::None || !Screen->Server_AllowViewerInput(Viewer->PlayerState))
	{
		return;
	}
	const bool bPointerKind = Input.Kind <= uint8(TSHostProtocol::EInput::Wheel) || Input.Kind == uint8(TSHostProtocol::EInput::MouseLeave);
	if (!bPointerKind && Scope == ETSInputScope::Pointer)
	{
		return;
	}
	const FString Name = Viewer->PlayerState->GetPlayerName();
	const APlayerState* ControllerState = Screen->GetController();
	APlayerController* Controller = ControllerState ? Cast<APlayerController>(ControllerState->GetOwner()) : nullptr;
	if (Controller && Controller->IsLocalController())
	{
		Screen->OnViewerInput.Broadcast(Input, Name, Scope);
	}
	else if (AFGPlayerController* Remote = Cast<AFGPlayerController>(Controller))
	{
		if (UTSRemoteCallObject* RCO = Remote->GetRemoteCallObjectOfClass<UTSRemoteCallObject>())
		{
			if (bReliable)
			{
				RCO->Client_ViewerInput(Screen, Input, Name, Scope);
			}
			else
			{
				RCO->Client_ViewerPointer(Screen, Input, Name, Scope);
			}
		}
	}
	// Everyone watching sees where the viewer points (at most 15 times a second).
	const double Now = FPlatformTime::Seconds();
	if (bPointerKind && Now - LastCursorBroadcast > 1.0 / 15.0)
	{
		LastCursorBroadcast = Now;
		BroadcastMarker(Screen, Name, Input.X, Input.Y, false);
	}
}

void UTSRemoteCallObject::Client_ViewerInput_Implementation(ATSBuildableScreen* Screen, const FTSViewerInput& Input, const FString& ViewerName, ETSInputScope Scope)
{
	if (Screen)
	{
		Screen->OnViewerInput.Broadcast(Input, ViewerName, Scope);
	}
}

void UTSRemoteCallObject::Client_ViewerPointer_Implementation(ATSBuildableScreen* Screen, const FTSViewerInput& Input, const FString& ViewerName, ETSInputScope Scope)
{
	if (Screen)
	{
		Screen->OnViewerInput.Broadcast(Input, ViewerName, Scope);
	}
}

bool UTSRemoteCallObject::Server_Ping_Validate(ATSBuildableScreen* Screen, uint16 X, uint16 Y)
{
	return true;
}

void UTSRemoteCallObject::Server_Ping_Implementation(ATSBuildableScreen* Screen, uint16 X, uint16 Y)
{
	const APlayerController* Viewer = GetOwnerPlayerController();
	if (!Screen || !Viewer || !Viewer->PlayerState || Screen->GetMode() == ETSScreenMode::SyncedMedia)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	RecentPings.RemoveAll([Now](double T) { return Now - T > 1.0; });
	if (RecentPings.Num() >= 3)
	{
		return;
	}
	RecentPings.Add(Now);
	BroadcastMarker(Screen, Viewer->PlayerState->GetPlayerName(), X, Y, true);
}

void UTSRemoteCallObject::Client_ScreenMarker_Implementation(ATSBuildableScreen* Screen, const FString& Name, uint16 X, uint16 Y, bool bPing)
{
	if (Screen)
	{
		Screen->OnScreenMarker.Broadcast(Name, X / 65535.f, Y / 65535.f, bPing);
	}
}

void UTSRemoteCallObject::BroadcastMarker(ATSBuildableScreen* Screen, const FString& Name, uint16 X, uint16 Y, bool bPing)
{
	TArray<APlayerController*> Targets;
	for (const TWeakObjectPtr<APlayerController>& Watcher : Screen->GetWatchers())
	{
		if (Watcher.IsValid())
		{
			Targets.AddUnique(Watcher.Get());
		}
	}
	if (const APlayerState* ControllerState = Screen->GetController())
	{
		if (APlayerController* Controller = Cast<APlayerController>(ControllerState->GetOwner()))
		{
			Targets.AddUnique(Controller);
		}
	}
	for (APlayerController* Target : Targets)
	{
		if (Target->IsLocalController())
		{
			Screen->OnScreenMarker.Broadcast(Name, X / 65535.f, Y / 65535.f, bPing);
		}
		else if (AFGPlayerController* Remote = Cast<AFGPlayerController>(Target))
		{
			if (UTSRemoteCallObject* RCO = Remote->GetRemoteCallObjectOfClass<UTSRemoteCallObject>())
			{
				RCO->Client_ScreenMarker(Screen, Name, X, Y, bPing);
			}
		}
	}
}

void UTSRemoteCallObject::SendKeyframeRequest(ATSBuildableScreen* Screen)
{
	const APlayerState* ControllerState = Screen ? Screen->GetController() : nullptr;
	APlayerController* Controller = ControllerState ? Cast<APlayerController>(ControllerState->GetOwner()) : nullptr;
	if (!Controller)
	{
		return;
	}
	if (Controller->IsLocalController())
	{
		Screen->OnKeyframeRequested.Broadcast();
	}
	else if (AFGPlayerController* Remote = Cast<AFGPlayerController>(Controller))
	{
		if (UTSRemoteCallObject* RCO = Remote->GetRemoteCallObjectOfClass<UTSRemoteCallObject>())
		{
			RCO->Client_RequestKeyframe(Screen);
		}
	}
}

void UTSRemoteCallObject::Client_Notice_Implementation(const FString& Message)
{
	UE_LOG(LogTelescreen, Log, TEXT("Server notice: %s"), *Message);
	OnNotice.Broadcast(FText::FromString(Message));
}
