#include "Buildables/TSBuildableScreen.h"
#include "Components/StaticMeshComponent.h"
#include "Content/TSAssetPaths.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterPlayer.h"
#include "FGColoredInstanceMeshProxy.h"
#include "FGHUD.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Hologram/FGHologram.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Screen/TSScreenPlayer.h"
#include "TSConfig.h"
#include "TSRemoteCallObject.h"
#include "Telescreen.h"
#include "TimerManager.h"
#include "UI/TSScreenPanel.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	const FName ScreenEmissionParam(TEXT("Emission"));
	const FName ScreenGlossinessParam(TEXT("Glossiness"));
	const FName ScreenTintParam(TEXT("TintColorAndOpacity"));
	const FName ScreenBackgroundParam(TEXT("Background"));
	const FName ScreenOpacityFromTextureParam(TEXT("OpacityFromTexture"));
	const FName ScreenTextureParam(TEXT("SlateUI"));
	constexpr int32 MaxQueueLength = 100;
	constexpr double AdvanceGraceSeconds = 1.0;
}

ATSBuildableScreen::ATSBuildableScreen()
{
	mScreenPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenPlane"));
	mScreenPlane->SetupAttachment(RootComponent);
	mScreenPlane->SetMobility(EComponentMobility::Static);
	mScreenPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	mScreenPlane->SetCastShadow(false);
	mAllowColoring = false;
}

void ATSBuildableScreen::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATSBuildableScreen, mMediaState);
	DOREPLIFETIME(ATSBuildableScreen, mScreenName);
	DOREPLIFETIME(ATSBuildableScreen, mAspectMode);
	DOREPLIFETIME(ATSBuildableScreen, mScreenVolume);
	DOREPLIFETIME(ATSBuildableScreen, mMode);
	DOREPLIFETIME(ATSBuildableScreen, mController);
	DOREPLIFETIME(ATSBuildableScreen, mControllerName);
	DOREPLIFETIME(ATSBuildableScreen, mSourceUrl);
	DOREPLIFETIME(ATSBuildableScreen, mViewerCount);
	DOREPLIFETIME(ATSBuildableScreen, mCurtain);
	DOREPLIFETIME(ATSBuildableScreen, mViewerStates);
	DOREPLIFETIME(ATSBuildableScreen, mGrants);
	DOREPLIFETIME(ATSBuildableScreen, mStreamLimits);
}

void ATSBuildableScreen::ConfigureBillboardLayout(const FVector& MeshLocation, float MeshScale, const FVector& PlaneLocation,
	const FVector2D& PlaneSize, const FVector2D& WorldDimensions, const FVector& PoleOffset, const FVector2D& PoleScale, float SignToSignOffset,
	const FBox& ClearanceBox, const FVector& ClearanceOffset)
{
	mMeshComponentProxy->SetRelativeLocation(MeshLocation);
	mMeshComponentProxy->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
	mMeshComponentProxy->SetRelativeScale3D(FVector(MeshScale));

	// The engine plane is 100 x 100 units; roll 90 stands it up facing the sign's front, as the vanilla ProxyMesh.
	mScreenPlane->SetRelativeLocation(PlaneLocation);
	mScreenPlane->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));
	mScreenPlane->SetRelativeScale3D(FVector(PlaneSize.X / 100.0, PlaneSize.Y / 100.0, 1.0));

	mWorldDimensions = WorldDimensions;
	mPoleOffset = PoleOffset;
	mPoleScale = PoleScale;
	mSignToSignOffset = SignToSignOffset;

	FFGClearanceData Clearance;
	Clearance.Type = EClearanceType::CT_Soft;
	Clearance.ClearanceBox = ClearanceBox;
	Clearance.RelativeTransform = FTransform(ClearanceOffset);
	mClearanceData = {Clearance};
}

void ATSBuildableScreen::ResolveVanillaAssets()
{
	GetMutableDefault<ATSBuildableScreenSmall>()->ApplyVanillaAssets(TSAssetPaths::HologramSmallBillboard);
	GetMutableDefault<ATSBuildableScreenLarge>()->ApplyVanillaAssets(TSAssetPaths::HologramLargeBillboard);
}

void ATSBuildableScreen::ApplyVanillaAssets(const TCHAR* HologramPath)
{
	UStaticMesh* BillboardMesh = LoadObject<UStaticMesh>(nullptr, TSAssetPaths::BillboardMesh);
	UMaterialInterface* Background = LoadObject<UMaterialInterface>(nullptr, TSAssetPaths::SignBackgroundMaterial);
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TSAssetPaths::EnginePlane);
	UMaterialInterface* ScreenMaterial = LoadObject<UMaterialInterface>(nullptr, TSAssetPaths::SignWidgetMaterial);
	UClass* Hologram = LoadClass<AFGHologram>(nullptr, HologramPath);

	if (BillboardMesh)
	{
		if (!mMeshComponentProxy->SetStaticMesh(BillboardMesh))
		{
			UE_LOG(LogTelescreen, Error, TEXT("%s: SetStaticMesh refused the billboard mesh on the class default"), *GetClass()->GetName());
		}
		if (Background)
		{
			mMeshComponentProxy->SetMaterial(1, Background);
		}
	}
	if (Plane && !mScreenPlane->SetStaticMesh(Plane))
	{
		UE_LOG(LogTelescreen, Error, TEXT("%s: SetStaticMesh refused the screen plane on the class default"), *GetClass()->GetName());
	}
	if (ScreenMaterial)
	{
		mScreenMaterial = ScreenMaterial;
	}
	if (Hologram)
	{
		mHologramClass = Hologram;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: billboard mesh %s, background %s, plane %s, screen material %s, hologram %s"), *GetClass()->GetName(),
		BillboardMesh ? TEXT("ok") : TEXT("MISSING"), Background ? TEXT("ok") : TEXT("MISSING"), Plane ? TEXT("ok") : TEXT("MISSING"),
		ScreenMaterial ? TEXT("ok") : TEXT("MISSING"), Hologram ? *Hologram->GetName() : TEXT("MISSING"));
}

void ATSBuildableScreen::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		CopyAssetsFromClassDefault();
	}
}

void ATSBuildableScreen::CopyAssetsFromClassDefault()
{
	const ATSBuildableScreen* Defaults = GetClass()->GetDefaultObject<ATSBuildableScreen>();
	if (!Defaults || Defaults == this)
	{
		return;
	}
	mScreenMaterial = Defaults->mScreenMaterial;
	if (mMeshComponentProxy && Defaults->mMeshComponentProxy)
	{
		if (UStaticMesh* Mesh = Defaults->mMeshComponentProxy->GetStaticMesh(); Mesh && !mMeshComponentProxy->SetStaticMesh(Mesh))
		{
			UE_LOG(LogTelescreen, Error, TEXT("%s: the billboard mesh could not be set"), *GetName());
		}
		const TArray<TObjectPtr<UMaterialInterface>>& Materials = Defaults->mMeshComponentProxy->OverrideMaterials;
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			if (Materials[Index])
			{
				mMeshComponentProxy->SetMaterial(Index, Materials[Index]);
			}
		}
	}
	if (mScreenPlane && Defaults->mScreenPlane)
	{
		if (UStaticMesh* Plane = Defaults->mScreenPlane->GetStaticMesh(); Plane && !mScreenPlane->SetStaticMesh(Plane))
		{
			UE_LOG(LogTelescreen, Error, TEXT("%s: the screen plane could not be set"), *GetName());
		}
	}
}

void ATSBuildableScreen::BeginPlay()
{
	Super::BeginPlay();

	const FVector2D Size = GetScreenWorldSize();
	UE_LOG(LogTelescreen, Log, TEXT("%s began play at %s, screen %.0f x %.0f cm, %d queued, playing %d (billboard %s, plane %s, material %s)"), *GetName(),
		*GetActorLocation().ToCompactString(), Size.X, Size.Y, mMediaState.Queue.Num(), mMediaState.CurrentIndex,
		mMeshComponentProxy && mMeshComponentProxy->GetStaticMesh() ? TEXT("ok") : TEXT("MISSING"), mScreenPlane && mScreenPlane->GetStaticMesh() ? TEXT("ok") : TEXT("MISSING"),
		mScreenMaterial ? TEXT("ok") : TEXT("MISSING"));

	if (HasAuthority())
	{
		ScheduleAdvance();
		GetWorldTimerManager().SetTimer(mControllerCheckTimer, this, &ATSBuildableScreen::CheckController, 2.0f, true);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!mScreenMaterial)
	{
		UE_LOG(LogTelescreen, Error, TEXT("%s: no screen material; the face stays blank"), *GetName());
		return;
	}
	mScreenMID = UMaterialInstanceDynamic::Create(mScreenMaterial, this);
	mScreenMID->SetScalarParameterValue(ScreenEmissionParam, 1.0f);
	mScreenMID->SetScalarParameterValue(ScreenGlossinessParam, 0.0f);
	mScreenMID->SetScalarParameterValue(ScreenOpacityFromTextureParam, 0.0f);
	mScreenMID->SetVectorParameterValue(ScreenTintParam, FLinearColor::White);
	mScreenMID->SetVectorParameterValue(ScreenBackgroundParam, FLinearColor::Black);
	mScreenPlane->SetMaterial(0, mScreenMID);

	mPlayer = NewObject<UTSScreenPlayer>(this, TEXT("TelescreenPlayer"));
	mPlayer->RegisterComponent();
}

void ATSBuildableScreen::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(mAdvanceTimer);
	GetWorldTimerManager().ClearTimer(mControllerCheckTimer);
	if (mPlayer)
	{
		mPlayer->DestroyComponent();
		mPlayer = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ATSBuildableScreen::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	Super::PreSaveGame_Implementation(SaveVersion, GameVersion);
	// World time restarts with every session, so the save keeps the position instead of the start time.
	if (!mMediaState.bPaused)
	{
		mMediaState.PausedPosition = GetPlaybackPosition();
	}
}

void ATSBuildableScreen::PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	Super::PostLoadGame_Implementation(SaveVersion, GameVersion);
	if (!mMediaState.Queue.IsValidIndex(mMediaState.CurrentIndex))
	{
		mMediaState.CurrentIndex = INDEX_NONE;
	}
	mMediaState.StartServerTime = GetServerTime() - mMediaState.PausedPosition;
	UE_LOG(LogTelescreen, Log, TEXT("%s loaded: %d queued, item %d at %.1f s%s"), *GetName(), mMediaState.Queue.Num(), mMediaState.CurrentIndex,
		mMediaState.PausedPosition, mMediaState.bPaused ? TEXT(" (paused)") : TEXT(""));
}

bool ATSBuildableScreen::IsUseable_Implementation() const
{
	return true;
}

void ATSBuildableScreen::OnUse_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State)
{
	APlayerController* Controller = ByCharacter ? Cast<APlayerController>(ByCharacter->GetController()) : nullptr;
	if (!Controller || !Controller->IsLocalPlayerController())
	{
		return;
	}
	if (AFGHUD* Hud = Cast<AFGHUD>(Controller->GetHUD()))
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: opening the screen panel"), *GetName());
		Hud->OpenInteractUI(UTSScreenPanel::StaticClass(), this);
	}
	else
	{
		UE_LOG(LogTelescreen, Warning, TEXT("%s: no FGHUD to open the screen panel on"), *GetName());
	}
}

FText ATSBuildableScreen::GetLookAtDecription_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) const
{
	return GetDisplayName();
}

FText ATSBuildableScreen::GetDisplayName() const
{
	return mScreenName.IsEmpty() ? mDisplayName : FText::FromString(mScreenName);
}

void ATSBuildableScreen::SetScreenTexture(UTexture* Texture)
{
	if (mScreenMID && Texture)
	{
		mScreenMID->SetTextureParameterValue(ScreenTextureParam, Texture);
	}
}

FVector2D ATSBuildableScreen::GetScreenWorldSize() const
{
	const FVector Scale = mScreenPlane->GetComponentScale();
	return FVector2D(100.0 * FMath::Abs(Scale.X), 100.0 * FMath::Abs(Scale.Y));
}

double ATSBuildableScreen::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void ATSBuildableScreen::OnRep_MediaState()
{
	UE_LOG(LogTelescreen, Verbose, TEXT("%s: media state revision %d (item %d, %s)"), *GetName(), mMediaState.Revision, mMediaState.CurrentIndex,
		mMediaState.bPaused ? TEXT("paused") : TEXT("playing"));
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::OnRep_ScreenSettings()
{
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::OnRep_Control()
{
	UE_LOG(LogTelescreen, Log, TEXT("%s: mode %d, controller %s"), *GetName(), int32(mMode), *mControllerName);
	OnScreenChanged.Broadcast();
}

bool ATSBuildableScreen::IsLocallyControlled() const
{
	const APlayerController* Local = UGameplayStatics::GetPlayerController(this, 0);
	return mController && Local && Local->PlayerState == mController;
}

void ATSBuildableScreen::Server_TakeControl(APlayerState* Requester, ETSScreenMode Mode, const FString& Url)
{
	if (!HasAuthority() || !Requester)
	{
		return;
	}
	const FString Previous = mControllerName;
	mMode = Mode;
	mController = Requester;
	mControllerName = Requester->GetPlayerName();
	mSourceUrl = Url.Left(2048);
	mWatchers.RemoveAll([Requester](const TWeakObjectPtr<APlayerController>& P) { return !P.IsValid() || P->PlayerState == Requester; });
	SyncViewers();
	mCurtain = false;
	mGrants.Reset();
	RefreshStreamLimits();
	UE_LOG(LogTelescreen, Log, TEXT("%s: %s took control (mode %d, %s)%s"), *GetName(), *mControllerName, int32(Mode), *mSourceUrl,
		Previous.IsEmpty() || Previous == mControllerName ? TEXT("") : *FString::Printf(TEXT(" from %s"), *Previous));
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::Server_ReleaseControl(const TCHAR* Reason)
{
	if (!HasAuthority() || mMode == ETSScreenMode::SyncedMedia)
	{
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: %s released control (%s)"), *GetName(), *mControllerName, Reason);
	mMode = ETSScreenMode::SyncedMedia;
	mController = nullptr;
	mControllerName.Reset();
	mSourceUrl.Reset();
	mWatchers.Reset();
	SyncViewers();
	mGrants.Reset();
	mCurtain = false;
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::Server_SetWatcher(APlayerController* Watcher, bool bWatching)
{
	if (!HasAuthority() || !Watcher || mMode == ETSScreenMode::SyncedMedia)
	{
		return;
	}
	const bool bIsController = Watcher->PlayerState == mController;
	bool bChanged = false;
	if (bWatching && !bIsController && !mWatchers.Contains(Watcher))
	{
		mWatchers.Add(Watcher);
		bChanged = true;
	}
	else if (!bWatching)
	{
		bChanged = mWatchers.Remove(Watcher) > 0;
	}
	if (!bChanged)
	{
		return;
	}
	SyncViewers();
	UE_LOG(LogTelescreen, Log, TEXT("%s: %s %s the stream (%d viewer(s))"), *GetName(), Watcher->PlayerState ? *Watcher->PlayerState->GetPlayerName() : TEXT("?"),
		bWatching ? TEXT("watches") : TEXT("stopped watching"), mViewerCount);
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
	if (bWatching)
	{
		UTSRemoteCallObject::SendKeyframeRequest(this);
	}
}

void ATSBuildableScreen::Server_SetCurtain(bool bOn)
{
	if (!HasAuthority() || mCurtain == bOn)
	{
		return;
	}
	mCurtain = bOn;
	UE_LOG(LogTelescreen, Log, TEXT("%s: privacy curtain %s"), *GetName(), bOn ? TEXT("on") : TEXT("off"));
	// The curtain is also the panic key: nobody keeps control of the source while it is blanked.
	if (bOn)
	{
		Server_RevokeGrants(TEXT("curtain on"));
	}
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
	if (!bOn)
	{
		UTSRemoteCallObject::SendKeyframeRequest(this);
	}
}

void ATSBuildableScreen::SyncViewers()
{
	mViewerCount = mWatchers.Num();
	mViewerStates.Reset();
	for (const TWeakObjectPtr<APlayerController>& Watcher : mWatchers)
	{
		if (Watcher.IsValid() && Watcher->PlayerState)
		{
			mViewerStates.Add(Watcher->PlayerState);
		}
	}
	// Viewers who left lose their grant.
	mGrants.RemoveAll([this](const FTSInputGrant& Grant) { return !Grant.Player || !mViewerStates.Contains(Grant.Player); });
}

ETSInputScope ATSBuildableScreen::GetGrantFor(const APlayerState* Player) const
{
	const FTSInputGrant* Grant = Player ? mGrants.FindByPredicate([Player](const FTSInputGrant& Each) { return Each.Player == Player; }) : nullptr;
	return Grant && Grant->ExpiresAt > GetServerTime() ? Grant->Scope : ETSInputScope::None;
}

ETSInputScope ATSBuildableScreen::GetMaxGrantScope() const
{
	const FString& Key = mMode == ETSScreenMode::Window ? UTSConfig::ViewerInputWindowKey
		: mMode == ETSScreenMode::RemoteDesktop			? UTSConfig::ViewerInputRemoteKey
														: UTSConfig::ViewerInputBrowserKey;
	const int32 Default = mMode == ETSScreenMode::Browser ? int32(ETSInputScope::Pointer) : int32(ETSInputScope::None);
	ETSInputScope Max = ETSInputScope(FMath::Clamp(UTSConfig::GetInt(this, Key, Default), 0, 3));
	// Full keyboard only in the browser, whose shortcuts the input filter knows.
	if (mMode != ETSScreenMode::Browser && Max == ETSInputScope::Full)
	{
		Max = ETSInputScope::Typing;
	}
	return mMode == ETSScreenMode::SyncedMedia ? ETSInputScope::None : Max;
}

void ATSBuildableScreen::Server_SetGrant(APlayerState* Player, ETSInputScope Scope, int32 Minutes)
{
	if (!HasAuthority() || !Player || Player == mController)
	{
		return;
	}
	mGrants.RemoveAll([Player](const FTSInputGrant& Each) { return Each.Player == Player; });
	const ETSInputScope Granted = ETSInputScope(FMath::Min(uint8(Scope), uint8(GetMaxGrantScope())));
	if (Granted != ETSInputScope::None && mViewerStates.Contains(Player))
	{
		FTSInputGrant& Grant = mGrants.AddDefaulted_GetRef();
		Grant.Player = Player;
		Grant.PlayerName = Player->GetPlayerName();
		Grant.Scope = Granted;
		Grant.ExpiresAt = GetServerTime() + 60.0 * FMath::Clamp(Minutes, 1, 120);
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: %s %s %s input (%s, %d min)"), *GetName(), *mControllerName, Granted == ETSInputScope::None ? TEXT("revoked") : TEXT("granted"),
		*Player->GetPlayerName(), *UEnum::GetValueAsString(Granted), Minutes);
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::Server_RevokeGrants(const TCHAR* Reason)
{
	if (!HasAuthority() || mGrants.Num() == 0)
	{
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: all input grants revoked (%s)"), *GetName(), Reason);
	mGrants.Reset();
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

bool ATSBuildableScreen::Server_AllowViewerInput(const APlayerState* Viewer)
{
	// 120 events a second at most per viewer.
	TArray<double>& Times = mViewerInputTimes.FindOrAdd(Viewer);
	const double Now = FPlatformTime::Seconds();
	Times.RemoveAll([Now](double T) { return Now - T > 1.0; });
	if (Times.Num() >= 120)
	{
		return false;
	}
	Times.Add(Now);
	return true;
}

bool ATSBuildableScreen::Server_AllowKeyframeRequest()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - mLastKeyframeRequest < 0.25)
	{
		return false;
	}
	mLastKeyframeRequest = Now;
	return true;
}

FVector ATSBuildableScreen::GetScreenCenter() const
{
	return mScreenPlane ? mScreenPlane->GetComponentLocation() : GetActorLocation();
}

bool ATSBuildableScreen::Server_AllowStreamBytes(int32 Bytes)
{
	const double KbpsCap = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxStreamKbpsKey, 8000), 500, 100000);
	const double BytesPerSecond = KbpsCap * 125.0;
	const double Now = FPlatformTime::Seconds();
	mStreamBudget = FMath::Min(BytesPerSecond * 0.5, mStreamBudget + (Now - mStreamBudgetAt) * BytesPerSecond);
	mStreamBudgetAt = Now;
	if (mStreamBudget < Bytes)
	{
		return false;
	}
	mStreamBudget -= Bytes;
	return true;
}

bool ATSBuildableScreen::RefreshStreamLimits()
{
	FTSStreamLimits Limits;
	Limits.MaxKbps = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxStreamKbpsKey, 8000), 500, 100000);
	Limits.MaxHeight = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxStreamHeightKey, 1080), 240, 2160);
	Limits.MaxFps = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::MaxStreamFpsKey, 60), 10, 60);
	if (Limits == mStreamLimits)
	{
		return false;
	}
	mStreamLimits = Limits;
	UE_LOG(LogTelescreen, Log, TEXT("%s: stream limits %d kbps, %d lines, %d fps"), *GetName(), Limits.MaxKbps, Limits.MaxHeight, Limits.MaxFps);
	return true;
}

void ATSBuildableScreen::CheckController()
{
	if (mWatchers.RemoveAll([](const TWeakObjectPtr<APlayerController>& P) { return !P.IsValid(); }) > 0)
	{
		SyncViewers();
		FlushNetDormancy();
		ForceNetUpdate();
		OnScreenChanged.Broadcast();
	}
	if (mMode != ETSScreenMode::SyncedMedia && !IsValid(mController))
	{
		Server_ReleaseControl(TEXT("controller left"));
	}
	if (mMode != ETSScreenMode::SyncedMedia && RefreshStreamLimits())
	{
		FlushNetDormancy();
		ForceNetUpdate();
	}
	const double Now = GetServerTime();
	if (mGrants.RemoveAll([Now](const FTSInputGrant& Grant) { return Grant.ExpiresAt <= Now; }) > 0)
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: an input grant expired"), *GetName());
		FlushNetDormancy();
		ForceNetUpdate();
		OnScreenChanged.Broadcast();
	}
}

void ATSBuildableScreen::CommitMediaState()
{
	++mMediaState.Revision;
	FlushNetDormancy();
	ForceNetUpdate();
	ScheduleAdvance();
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::ScheduleAdvance()
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(mAdvanceTimer);
	const FTSMediaItem* Current = mMediaState.GetCurrent();
	if (!Current || mMediaState.bPaused || Current->bIsLive || Current->Duration <= 0.0f)
	{
		return;
	}
	const double Remaining = FMath::Max(0.1, Current->Duration - GetPlaybackPosition() + AdvanceGraceSeconds);
	Timers.SetTimer(mAdvanceTimer, this, &ATSBuildableScreen::OnCurrentItemFinished, float(Remaining), false);
}

void ATSBuildableScreen::OnCurrentItemFinished()
{
	UE_LOG(LogTelescreen, Log, TEXT("%s: item %d finished"), *GetName(), mMediaState.CurrentIndex);
	ApplyMediaCommand(ETSMediaCommand::Next, 0, 0.0);
}

void ATSBuildableScreen::StartItem(int32 Index, double Position)
{
	mMediaState.CurrentIndex = mMediaState.Queue.IsValidIndex(Index) ? Index : INDEX_NONE;
	mMediaState.bPaused = false;
	mMediaState.PausedPosition = 0.0;
	mMediaState.StartServerTime = GetServerTime() - Position;
}

void ATSBuildableScreen::Server_Enqueue(const FTSMediaItem& Item, bool bPlayNow)
{
	if (!HasAuthority())
	{
		return;
	}
	if (mMediaState.Queue.Num() >= MaxQueueLength)
	{
		// Drops the oldest entry that isn't playing.
		const int32 Oldest = mMediaState.CurrentIndex == 0 ? 1 : 0;
		mMediaState.Queue.RemoveAt(Oldest);
		if (mMediaState.CurrentIndex > Oldest)
		{
			--mMediaState.CurrentIndex;
		}
	}
	const int32 Index = mMediaState.Queue.Add(Item);
	UE_LOG(LogTelescreen, Log, TEXT("%s: %s queued \"%s\" (%s, %.0f s)%s"), *GetName(), *Item.AddedBy, *Item.GetDisplayTitle(), *Item.Url, Item.Duration,
		bPlayNow ? TEXT(", playing now") : TEXT(""));
	if (bPlayNow || mMediaState.CurrentIndex == INDEX_NONE)
	{
		StartItem(Index, 0.0);
	}
	CommitMediaState();
}

void ATSBuildableScreen::ApplyMediaCommand(ETSMediaCommand Command, int32 Index, double Seconds)
{
	if (!HasAuthority())
	{
		return;
	}
	FTSMediaState& S = mMediaState;
	switch (Command)
	{
	case ETSMediaCommand::PlayIndex:
		if (!S.Queue.IsValidIndex(Index)) return;
		StartItem(Index, 0.0);
		break;
	case ETSMediaCommand::Remove:
		if (!S.Queue.IsValidIndex(Index)) return;
		S.Queue.RemoveAt(Index);
		if (Index == S.CurrentIndex)
		{
			StartItem(S.Queue.IsValidIndex(Index) ? Index : INDEX_NONE, 0.0);
		}
		else if (Index < S.CurrentIndex)
		{
			--S.CurrentIndex;
		}
		break;
	case ETSMediaCommand::Pause:
		if (S.bPaused || !S.GetCurrent()) return;
		S.PausedPosition = S.GetPosition(GetServerTime());
		S.bPaused = true;
		break;
	case ETSMediaCommand::Resume:
		if (!S.bPaused) return;
		S.StartServerTime = GetServerTime() - S.PausedPosition;
		S.bPaused = false;
		break;
	case ETSMediaCommand::Seek:
	{
		const FTSMediaItem* Current = S.GetCurrent();
		if (!Current || Current->bIsLive) return;
		const double Clamped = FMath::Clamp(Seconds, 0.0, Current->Duration > 0.0f ? double(Current->Duration) : Seconds);
		if (S.bPaused)
		{
			S.PausedPosition = Clamped;
		}
		else
		{
			S.StartServerTime = GetServerTime() - Clamped;
		}
		break;
	}
	case ETSMediaCommand::Next:
		StartItem(S.Queue.IsValidIndex(S.CurrentIndex + 1) ? S.CurrentIndex + 1 : INDEX_NONE, 0.0);
		break;
	case ETSMediaCommand::Stop:
		StartItem(INDEX_NONE, 0.0);
		break;
	case ETSMediaCommand::Clear:
		S.Queue.Reset();
		StartItem(INDEX_NONE, 0.0);
		break;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: command %d (index %d, %.1f s) -> item %d, %s at %.1f s"), *GetName(), int32(Command), Index, Seconds,
		S.CurrentIndex, S.bPaused ? TEXT("paused") : TEXT("playing"), S.GetPosition(GetServerTime()));
	CommitMediaState();
}

void ATSBuildableScreen::Server_SetScreenSettings(const FString& Name, ETSAspectMode AspectMode)
{
	if (!HasAuthority())
	{
		return;
	}
	mScreenName = Name.Left(64).TrimStartAndEnd();
	mAspectMode = AspectMode;
	UE_LOG(LogTelescreen, Log, TEXT("%s: renamed to \"%s\", aspect %d"), *GetName(), *mScreenName, int32(mAspectMode));
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

void ATSBuildableScreen::Server_SetScreenVolume(float Volume)
{
	if (!HasAuthority())
	{
		return;
	}
	mScreenVolume = FMath::Clamp(Volume, 0.f, 1.f);
	UE_LOG(LogTelescreen, Log, TEXT("%s: screen volume %.0f%%"), *GetName(), mScreenVolume * 100.f);
	FlushNetDormancy();
	ForceNetUpdate();
	OnScreenChanged.Broadcast();
}

ATSBuildableScreenSmall::ATSBuildableScreenSmall()
{
	mDisplayName = LOCTEXT("SmallName", "Small Telescreen");
	mDescription = LOCTEXT("SmallDescription",
		"A screen for synced videos, a built-in browser and shared windows, seen by every player.\n\n"
		"Can be freestanding, placed on Walls, or attached to most buildings.\n\nSize: 8 m x 4 m");
	ConfigureBillboardLayout(FVector(0.0, -18.0, 0.0), 1.143f, FVector(0.0, 10.0, 0.0), FVector2D(795.0, 395.0), FVector2D(800.0, 400.0),
		FVector(0.0, 38.0, 0.0), FVector2D(1.0, 1.0), 0.0f, FBox(FVector(-400.0, -10.0, -200.0), FVector(400.0, 10.0, 200.0)), FVector(0.0, -2.0, 0.0));
}

ATSBuildableScreenLarge::ATSBuildableScreenLarge()
{
	mDisplayName = LOCTEXT("LargeName", "Large Telescreen");
	mDescription = LOCTEXT("LargeDescription",
		"A screen for synced videos, a built-in browser and shared windows, seen by every player.\n\n"
		"Can be freestanding, placed on Walls, or attached to most buildings.\n\nSize: 16 m x 8 m");
	ConfigureBillboardLayout(FVector(0.0, 15.0, 0.0), 2.29f, FVector(0.0, 66.719, 0.0), FVector2D(1600.0, 800.0), FVector2D(1600.0, 800.0),
		FVector(0.0, 25.0, 0.0), FVector2D(2.0, 2.0), 60.0f, FBox(FVector(-800.0, -20.0, -400.0), FVector(800.0, 20.0, 400.0)), FVector(0.0, 46.0, 0.0));
}

#undef LOCTEXT_NAMESPACE
