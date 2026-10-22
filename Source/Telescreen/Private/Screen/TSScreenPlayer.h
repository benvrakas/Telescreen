#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Host/TSHostSubsystem.h"
#include "Screen/TSSharedBuffers.h"
#include "TSScreenPlayer.generated.h"

class ATSBuildableScreen;
class UAudioComponent;
class UCanvas;
class UCanvasRenderTarget2D;
class UTexture2D;
class UTexture;
class UTSStreamingSoundWave;

// Plays a screen's synced media on this client: helper session, video texture, 3D audio, status text. Not on dedicated
// servers.
UCLASS()
class UTSScreenPlayer : public UActorComponent, public ITSHostSessionListener
{
	GENERATED_BODY()

public:
	UTSScreenPlayer();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void OnHostVideo(uint32 Generation, uint32 Width, uint32 Height, const FString& MappingName) override;
	virtual void OnHostAudio(uint32 SampleRate, uint32 Channels, uint32 CapacityFrames, const FString& MappingName) override;
	virtual void OnHostState(TSHostProtocol::ESessionState State, double Position, double Duration, const FString& Message) override;
	virtual void OnHostLost() override;

	// What this client shows instead of the video right now, or empty while the video plays.
	FText GetLocalStatus() const { return bShowingVideo ? FText::GetEmpty() : StatusLine2; }

	// Why the current item isn't loading on this PC, or empty.
	FText GetBlockedReason() const { return BlockedReason; }

	// Unblocks the current item on this PC although the privacy setting would skip it.
	void LoadAnyway();

	// Keeps the screen playing regardless of distance and switches its audio for the HUD picture.
	void SetPinned(bool bInPinned);
	bool IsPinned() const { return bPinned; }

	// The texture the screen shows now: the video, or the status card.
	UTexture* GetDisplayTexture() const;

private:
	ATSBuildableScreen* GetScreen() const;
	void OnScreenChanged();
	void Reconcile();
	void OpenSession(const struct FTSMediaItem& Item);
	void CloseSession();
	void PumpVideo();
	void UpdateAudio();
	void ApplyAudioMode();
	void RankActivity();
	bool ShouldLoad(const struct FTSMediaItem& Item, FText& OutReason) const;

	void SetStatus(const FText& Line1, const FText& Line2);
	void ShowStatus();
	UFUNCTION()
	void DrawStatus(UCanvas* Canvas, int32 Width, int32 Height);

	uint32 Session = 0;
	FString SessionKey;
	FString WantedKey;
	FString UnblockedKey;
	FText BlockedReason;

	FTSVideoReader Video;
	TArray<uint8> FrameBuffer;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VideoTexture;
	bool bShowingVideo = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Audio;
	UPROPERTY(Transient)
	TObjectPtr<UTSStreamingSoundWave> Wave;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> StatusTarget;
	FText StatusLine1, StatusLine2;

	TSHostProtocol::ESessionState HostState = TSHostProtocol::ESessionState::Resolving;
	bool bActive = false;
	bool bPinned = false;
	double NextSync = 0.0;
	double NextRank = 0.0;
	double NextAudioUpdate = 0.0;
	FDelegateHandle ScreenChangedHandle;
};
