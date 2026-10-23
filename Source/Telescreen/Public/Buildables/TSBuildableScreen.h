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
class APlayerState;

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
DECLARE_MULTICAST_DELEGATE_OneParam(FTSOnStreamChunk, const FTSStreamChunk&);
DECLARE_MULTICAST_DELEGATE(FTSOnKeyframeRequested);
// Direct links: on the stream sender's client, a viewer asks for one; on the viewer's client, the sender answered.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FTSOnDirectSignal, uint32 /*Link*/, const TArray<uint8>& /*Key*/, const TArray<FString>& /*Candidates*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FTSOnViewerInput, const FTSViewerInput&, const FString& /*ViewerName*/, ETSInputScope);
DECLARE_MULTICAST_DELEGATE_FourParams(FTSOnScreenMarker, const FString& /*Name*/, float /*X*/, float /*Y*/, bool /*bPing*/);

// A Telescreen: a billboard-shaped sign whose face shows synced media.
UCLASS(Abstract)
class TELESCREEN_API ATSBuildableScreen : public AFGBuildableSignBase
{
	GENERATED_BODY()

public:
	ATSBuildableScreen();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitProperties() override;
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
	// The screen's own volume for everyone (0-1), on top of each player's settings.
	float GetScreenVolume() const { return mScreenVolume; }
	UTSScreenPlayer* GetPlayer() const { return mPlayer; }
	ETSScreenMode GetMode() const { return mMode; }
	APlayerState* GetController() const { return mController; }
	const FString& GetControllerName() const { return mControllerName; }
	const FString& GetSourceUrl() const { return mSourceUrl; }

	// How many players are watching the controller's stream, and whether the controller blanked it.
	int32 GetViewerCount() const { return mViewerCount; }
	bool IsCurtainOn() const { return mCurtain; }

	// The players watching the stream (replicated, for the controller's grant list).
	const TArray<TObjectPtr<APlayerState>>& GetViewerStates() const { return mViewerStates; }

	// The world host's stream limits (replicated).
	const FTSStreamLimits& GetStreamLimits() const { return mStreamLimits; }

	// Who gave, lost or was refused control of the shared source, and when (newest last; replicated, at most 12).
	const TArray<FString>& GetControlLog() const { return mControlLog; }

	// Current input grants (replicated); expired ones are removed on the server.
	const TArray<FTSInputGrant>& GetGrants() const { return mGrants; }

	// What Player may do in the source right now (None without a live grant).
	ETSInputScope GetGrantFor(const APlayerState* Player) const;

	// Server only: gives Player Scope for Minutes (None revokes); Scope is clamped to the host's limit for the mode.
	void Server_SetGrant(APlayerState* Player, ETSInputScope Scope, int32 Minutes);

	// Server only: removes every grant.
	void Server_RevokeGrants(const TCHAR* Reason);

	// The most the host lets viewers do in this screen's mode.
	ETSInputScope GetMaxGrantScope() const;

	// Server only: true while Viewer stays under the viewer input rate limit.
	bool Server_AllowViewerInput(const APlayerState* Viewer);

	// Delivered on the controller's client for each viewer input event the server let through.
	FTSOnViewerInput OnViewerInput;

	// Delivered on every client watching (and the controller) for viewer cursors and pings.
	FTSOnScreenMarker OnScreenMarker;

	// Synced media a player can't play (a file they don't have, a link that fails for them) is streamed to them by a
	// player who plays it, chosen by the server from every player's report (see Docs/Architecture.md, Streaming
	// synced media).
	APlayerState* GetMediaStreamer() const { return mMediaStreamer; }
	const FString& GetMediaStreamerName() const { return mMediaStreamerName; }
	bool IsLocalMediaStreamer() const;

	// Server only: Player can (or can't) play Url, the current item; bCanStream: they agree to stream it to others.
	void Server_ReportMedia(APlayerState* Player, const FString& Url, bool bHave, bool bCanStream);

	// Who sends this screen's live stream now: the controller of a browser, window or desktop, or the media streamer.
	APlayerState* GetStreamSender() const;

	FTSOnDirectSignal OnDirectRequest;
	FTSOnDirectSignal OnDirectAnswer;

	// Watchers receiving the stream through the server (not over a direct link); the sender skips the game path at 0.
	int32 GetRelayedViewerCount() const { return mRelayedViewerCount; }

	// Server only: Watcher now gets the stream directly (or through the server again).
	void Server_SetDirectWatcher(APlayerController* Watcher, bool bDirect);
	bool IsDirectWatcher(const APlayerController* Watcher) const;

	// Server only: a direct link being set up between a viewer and the stream sender.
	struct FDirectLink
	{
		TWeakObjectPtr<APlayerController> Viewer;
		TWeakObjectPtr<APlayerState> Sender;
		TArray<uint8> Key;
		double CreatedAt = 0.0;
	};
	TMap<uint32, FDirectLink> mDirectLinks;

	// Server only: the players watching the stream.
	const TArray<TWeakObjectPtr<APlayerController>>& GetWatchers() const { return mWatchers; }

	// Server only: starts or stops Watcher receiving the stream; the controller is asked for a keyframe.
	void Server_SetWatcher(APlayerController* Watcher, bool bWatching);

	// Server only: turns the privacy curtain on or off.
	void Server_SetCurtain(bool bOn);

	// Server only: true if Bytes more stream data fit the stream's rate limit right now.
	bool Server_AllowStreamBytes(int32 Bytes);

	// Server only: true at most four times a second, so viewers can't flood the controller with keyframe requests.
	bool Server_AllowKeyframeRequest();

	// Where the screen face's centre is in the world.
	FVector GetScreenCenter() const;

	// Delivered on the client (and on a listen server's own player) for each stream chunk, and when the controller's
	// stream needs a keyframe.
	FTSOnStreamChunk OnStreamChunk;
	FTSOnKeyframeRequested OnKeyframeRequested;

	// True if the local player controls this screen's source.
	bool IsLocallyControlled() const;
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
	void Server_SetScreenVolume(float Volume);
	// Server only: sets the current item's length when the queue didn't know it (Url must be the current item's).
	void Server_SetCurrentDuration(const FString& Url, float Seconds);

	// Server only: gives Requester control of the screen in Mode, starting at Url.
	void Server_TakeControl(APlayerState* Requester, ETSScreenMode Mode, const FString& Url);

	// Server only: hands the screen back to synced media.
	void Server_ReleaseControl(const TCHAR* Reason);

	// Broadcast on clients and the server whenever the media state or settings change.
	FTSOnScreenChanged OnScreenChanged;

	// Loads the vanilla billboard assets into the class defaults of both screen sizes.
	static void ResolveVanillaAssets();

protected:
	void ApplyVanillaAssets(const TCHAR* HologramPath);

	// Copies the vanilla assets ApplyVanillaAssets put on the class default onto this instance (native actors don't
	// copy class-default changes made after construction).
	void CopyAssetsFromClassDefault();

	void ConfigureBillboardLayout(const FVector& MeshLocation, float MeshScale, const FVector& PlaneLocation, const FVector2D& PlaneSize,
		const FVector2D& WorldDimensions, const FVector& PoleOffset, const FVector2D& PoleScale, float SignToSignOffset, const FBox& ClearanceBox,
		const FVector& ClearanceOffset);

	UFUNCTION()
	void OnRep_MediaState();

	UFUNCTION()
	void OnRep_ScreenSettings();

	UFUNCTION()
	void OnRep_Control();

	void CheckController();

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

	UPROPERTY(ReplicatedUsing = OnRep_ScreenSettings, SaveGame)
	float mScreenVolume = 1.f;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	ETSScreenMode mMode = ETSScreenMode::SyncedMedia;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	TObjectPtr<APlayerState> mController;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	FString mControllerName;

	// Where the controller's source started (Browser: the first page).
	UPROPERTY(ReplicatedUsing = OnRep_Control)
	FString mSourceUrl;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	int32 mViewerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	bool mCurtain = false;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	TArray<TObjectPtr<APlayerState>> mViewerStates;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	TArray<FTSInputGrant> mGrants;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	TArray<FString> mControlLog;
	void AddControlLog(const FString& Line);

	UPROPERTY(Replicated)
	FTSStreamLimits mStreamLimits;

	UPROPERTY(Replicated)
	int32 mRelayedViewerCount = 0;

	TArray<TWeakObjectPtr<APlayerController>> mDirectWatchers;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	TObjectPtr<APlayerState> mMediaStreamer;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	FString mMediaStreamerName;

	// Server: what each player reported for the current item (mMediaReportsUrl).
	struct FMediaReport
	{
		bool bHave = false;
		bool bCanStream = false;
	};
	TMap<TWeakObjectPtr<APlayerState>, FMediaReport> mMediaReports;
	FString mMediaReportsUrl;
	void ResetMediaReports(const FString& Url);
	void ChooseMediaStreamer();
	// Server: reads the limits from this world's settings; true when they changed.
	bool RefreshStreamLimits();

	// Recent viewer input times per player (server), for the rate limit.
	TMap<TWeakObjectPtr<const APlayerState>, TArray<double>> mViewerInputTimes;
	void SyncViewers();

	TArray<TWeakObjectPtr<APlayerController>> mWatchers;
	double mStreamBudget = 0.0;
	double mStreamBudgetAt = 0.0;
	double mLastKeyframeRequest = 0.0;

	FTimerHandle mAdvanceTimer;
	FTimerHandle mControllerCheckTimer;
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
