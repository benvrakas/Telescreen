#include "TSRemoteCallObject.h"
#include "FGPlayerController.h"
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
	const APlayerController* Controller = GetOwnerPlayerController();
	if (Screen->GetNetMode() == NM_DedicatedServer || (Controller && Controller->IsLocalController()))
	{
		return true;
	}
	Client_Notice(TEXT("Only the host can change screen videos on this world."));
	return false;
}

bool UTSRemoteCallObject::Server_Enqueue_Validate(ATSBuildableScreen* Screen, const FTSMediaItem& Item, bool bPlayNow)
{
	return Item.Url.Len() <= MaxUrlLength && Item.Title.Len() <= MaxTitleLength;
}

void UTSRemoteCallObject::Server_Enqueue_Implementation(ATSBuildableScreen* Screen, const FTSMediaItem& Item, bool bPlayNow)
{
	const FString Requester = GetRequesterName();
	UE_LOG(LogTelescreen, Log, TEXT("Server_Enqueue from %s on %s: %s"), *Requester, *GetNameSafe(Screen), *Item.Url);
	if (!MayEdit(Screen))
	{
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
	return Name.Len() <= MaxNameLength && uint8(AspectMode) <= uint8(ETSAspectMode::Stretch);
}

void UTSRemoteCallObject::Server_SetScreenSettings_Implementation(ATSBuildableScreen* Screen, const FString& Name, ETSAspectMode AspectMode)
{
	UE_LOG(LogTelescreen, Log, TEXT("Server_SetScreenSettings from %s on %s: \"%s\", aspect %d"), *GetRequesterName(), *GetNameSafe(Screen), *Name,
		int32(AspectMode));
	if (MayEdit(Screen))
	{
		Screen->Server_SetScreenSettings(Name, AspectMode);
	}
}

bool UTSRemoteCallObject::Server_TakeControl_Validate(ATSBuildableScreen* Screen, ETSScreenMode Mode, const FString& Url)
{
	return uint8(Mode) <= uint8(ETSScreenMode::Browser) && Url.Len() <= MaxUrlLength;
}

void UTSRemoteCallObject::Server_TakeControl_Implementation(ATSBuildableScreen* Screen, ETSScreenMode Mode, const FString& Url)
{
	APlayerController* Controller = GetOwnerPlayerController();
	UE_LOG(LogTelescreen, Log, TEXT("Server_TakeControl from %s on %s: mode %d, %s"), *GetRequesterName(), *GetNameSafe(Screen), int32(Mode), *Url);
	if (!Screen || !Controller || !Controller->PlayerState || Mode == ETSScreenMode::SyncedMedia)
	{
		return;
	}
	if (Mode == ETSScreenMode::Browser && !UTSConfig::GetBool(Screen, UTSConfig::AllowBrowserKey, true))
	{
		Client_Notice(TEXT("The built-in browser is turned off on this world."));
		return;
	}
	if (!MayEdit(Screen))
	{
		return;
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
	const bool bIsHost = Controller->IsLocalController() && Screen->GetNetMode() != NM_DedicatedServer;
	if (bIsController || bIsHost)
	{
		Screen->Server_ReleaseControl(bIsController ? TEXT("controller stopped") : TEXT("host stopped it"));
	}
}

bool UTSRemoteCallObject::Server_StreamChunk_Validate(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk)
{
	return Chunk.Data.Num() <= MaxChunkBytes && Chunk.Count >= 1 && Chunk.Count <= 256 && Chunk.Index < Chunk.Count && Chunk.Kind <= 1;
}

void UTSRemoteCallObject::Server_StreamChunk_Implementation(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk)
{
	const APlayerController* Sender = GetOwnerPlayerController();
	if (!Screen || !Sender || Screen->GetMode() != ETSScreenMode::Browser || Screen->GetController() != Sender->PlayerState || Screen->IsCurtainOn() ||
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
	SendKeyframeRequest(Screen);
}

void UTSRemoteCallObject::Client_RequestKeyframe_Implementation(ATSBuildableScreen* Screen)
{
	if (Screen)
	{
		Screen->OnKeyframeRequested.Broadcast();
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
