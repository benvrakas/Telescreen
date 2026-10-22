#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableSignBase.h"
#include "TSTypes.h"
#include "TSBuildableScreen.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture;
class UTSScreenPlayer;

// Server-side media queue commands (see ATSBuildableScreen::ApplyMediaCommand).
UENUM()
enum class ETSMediaCommand : uint8
{
	PlayIndex,
	Remove,
	Pause,
	Resume,
	Seek,
	Next,
	Stop,
	Clear,
};

DECLARE_MULTICAST_DELEGATE(FTSOnScreenChanged);

// A Telescreen: a billboard-shaped sign whose face shows synced media.
UCLASS(Abstract)
class TELESCREEN_API ATSBuildableScreen : public AFGBuildableSignBase
{
	GENERATED_BODY()

public:
	ATSBuildableScreen();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;

	virtual bool IsUseable_Implementation() const override;
	virtual void OnUse_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) override;
	virtual FText GetLookAtDecription_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) const override;

	// Shows Texture on the screen face.
	void SetScreenTexture(UTexture* Texture);

	// Size of the screen face in world units, including the actor's scale.
	FVector2D GetScreenWorldSize() const;

	const FTSMediaState& GetMediaState() const { return mMediaState; }
	const FString& GetScreenName() const { return mScreenName; }
	ETSAspectMode GetAspectMode() const { return mAspectMode; }
	UTSScreenPlayer* GetPlayer() const { return mPlayer; }
	FText GetDisplayName() const;

	// Server world time (the clock synced media is timed against).
	double GetServerTime() const;

	// Where the current item is now, in seconds.
	double GetPlaybackPosition() const { return mMediaState.GetPosition(GetServerTime()); }

	// Server only: adds Item to the queue, optionally playing it at once.
	void Server_Enqueue(const FTSMediaItem& Item, bool bPlayNow);

	// Server only: applies a queue or playback command. Index is used by PlayIndex and Remove, Seconds by Seek.
	void ApplyMediaCommand(ETSMediaCommand Command, int32 Index, double Seconds);

	// Server only: renames the screen and sets how pictures fit it.
	void Server_SetScreenSettings(const FString& Name, ETSAspectMode AspectMode);

	// Broadcast on clients and the server whenever the media state or settings change.
	FTSOnScreenChanged OnScreenChanged;

	// Loads the vanilla billboard assets into the class defaults of both screen sizes.
	static void ResolveVanillaAssets();

protected:
	void ApplyVanillaAssets(const TCHAR* HologramPath);

	void ConfigureBillboardLayout(const FVector& MeshLocation, float MeshScale, const FVector& PlaneLocation, const FVector2D& PlaneSize,
		const FVector2D& WorldDimensions, const FVector& PoleOffset, const FVector2D& PoleScale, float SignToSignOffset, const FBox& ClearanceBox,
		const FVector& ClearanceOffset);

	UFUNCTION()
	void OnRep_MediaState();

	UFUNCTION()
	void OnRep_ScreenSettings();

	// Marks the media state changed on the server and schedules the move to the next item.
	void CommitMediaState();
	void ScheduleAdvance();
	void OnCurrentItemFinished();
	void StartItem(int32 Index, double Position);

	UPROPERTY(VisibleAnywhere, Category = "Telescreen")
	TObjectPtr<UStaticMeshComponent> mScreenPlane;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> mScreenMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> mScreenMID;

	UPROPERTY(Transient)
	TObjectPtr<UTSScreenPlayer> mPlayer;

	UPROPERTY(ReplicatedUsing = OnRep_MediaState, SaveGame)
	FTSMediaState mMediaState;

	UPROPERTY(ReplicatedUsing = OnRep_ScreenSettings, SaveGame)
	FString mScreenName;

	UPROPERTY(ReplicatedUsing = OnRep_ScreenSettings, SaveGame)
	ETSAspectMode mAspectMode = ETSAspectMode::Letterbox;

	FTimerHandle mAdvanceTimer;
};

// Small Telescreen, built from the Small Billboard (8 m x 4 m).
UCLASS()
class TELESCREEN_API ATSBuildableScreenSmall : public ATSBuildableScreen
{
	GENERATED_BODY()

public:
	ATSBuildableScreenSmall();
};

// Large Telescreen, built from the Large Billboard (16 m x 8 m).
UCLASS()
class TELESCREEN_API ATSBuildableScreenLarge : public ATSBuildableScreen
{
	GENERATED_BODY()

public:
	ATSBuildableScreenLarge();
};
