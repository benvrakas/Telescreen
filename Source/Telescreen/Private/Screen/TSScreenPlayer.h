#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Host/TSHostSubsystem.h"
#include "Screen/TSSharedBuffers.h"
#include "TSTypes.h"
#include "TSScreenPlayer.generated.h"

class ATSBuildableScreen;
class UAudioComponent;
class USceneComponent;
class UCanvas;
class UCanvasRenderTarget2D;
class UTexture2D;
class UTexture;
class UTSStreamingSoundWave;
class UTSRemoteCallObject;
class FTSStreamSenderListener;

DECLARE_MULTICAST_DELEGATE(FTSOnBrowserStateChanged);

// Plays a screen's synced media, or the local controller's browser, on this client: helper session, video texture, 3D
// audio, status text. Not on dedicated servers.
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
	virtual void OnHostBrowserState(const FTSBrowserState& State) override;

	// What this client shows instead of the video right now, or empty while the video plays.
	FText GetLocalStatus() const { return bShowingVideo ? FText::GetEmpty() : StatusLine2; }

	// Applies a volume change (the panel's My volume slider) right away.
	void RefreshVolume() { UpdateAudio(); }

	// Why the current item isn't loading on this PC, or empty.
	FText GetBlockedReason() const { return BlockedReason; }

	// Unblocks the current item on this PC although the privacy setting would skip it.
	void LoadAnyway();

	// Keeps the screen playing regardless of distance and switches its audio for the HUD picture.
	void SetPinned(bool bInPinned);
	bool IsPinned() const { return bPinned; }

	// The texture the screen shows now: the video, or the status card.
	UTexture* GetDisplayTexture() const;

	// True while this client runs the screen's browser (the local player controls it).
	bool HasBrowser() const { return bBrowserSession && Session != 0; }

	// True while this client shares one of its windows on the screen.
	bool HasWindow() const { return bWindowSession && Session != 0; }

	// True while this client shows a remote desktop (VNC) on the screen.
	bool HasRemoteDesktop() const { return bRemoteSession && Session != 0; }

	// True if the window's mouse and keyboard can go to this screen's source (the local player's browser, window or
	// remote desktop).
	bool AcceptsInput() const { return HasBrowser() || HasWindow() || HasRemoteDesktop(); }
	const FTSBrowserState& GetBrowserState() const { return BrowserState; }
	FTSOnBrowserStateChanged OnBrowserStateChanged;

	// Browser input. Positions are 0-1 across the picture; Modifiers are TSHostProtocol::Mod* bits.
	void BrowserMouseMove(const FVector2D& Position, uint32 Modifiers);
	void BrowserMouseButton(const FVector2D& Position, uint8 Button, bool bUp, int32 ClickCount, uint32 Modifiers);
	void BrowserWheel(const FVector2D& Position, int32 DeltaX, int32 DeltaY, uint32 Modifiers);
	void BrowserKey(TSHostProtocol::EKeyType Type, int32 KeyCode, TCHAR Character, uint32 Modifiers);
	void BrowserText(const FString& Text);
	void BrowserPointerDelta(int32 DeltaX, int32 DeltaY);
	void BrowserMouseLeave();
	void BrowserFocus(bool bFocused);
	void BrowserNavigate(const FString& Url);
	void BrowserCommand(TSHostProtocol::ECommand Command);

	// M3 test: shows Source's browser on this screen through the stream encoder and decoder (on this PC only).
	void StartLoopback(UTSScreenPlayer* Source);
	void StopLoopback();
	bool IsLoopback() const { return LoopbackSource.IsValid(); }
	uint32 GetSessionId() const { return Session; }

	// Viewer control: true if the local player (watching someone else's source) may send input to it now.
	bool CanSendViewerInput() const;

	// True if the screen window may take mouse and keyboard for this screen: the local player's own source, or watching
	// someone else's (clicks become input with a grant, pings without).
	bool CanInteract() const { return AcceptsInput() || IsReceivingStream(); }
	ETSInputScope GetLocalGrant() const;

	// Cursors of viewers with control and pings, as shown on this PC (positions 0-1 across the picture).
	struct FMarker
	{
		FString Name;
		FVector2D Position;
		double Until = 0.0;
		bool bPing = false;
		FLinearColor Color;
	};
	const TArray<FMarker>& GetMarkers() const { return Markers; }

	// Live stream: text about the running stream for the panel (controller: encoder, rate; viewer: decoder, rate).
	FText GetStreamStats() const { return StreamStats; }
	bool IsStreaming() const { return SenderSession != 0; }
	bool IsReceivingStream() const { return bStreamReceiver && Session != 0; }

	// Called by the sender's listener (game thread).
	void SendStreamChunks(uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, const TArray<uint8>& Data);
	void OnSenderState(const FString& Message);
	void OnSenderLost();

private:
	ATSBuildableScreen* GetScreen() const;
	void OnScreenChanged();
	void Reconcile();
	void OpenSession(const struct FTSMediaItem& Item);
	void OpenBrowserSession();
	void OpenWindowSession();
	void OpenRemoteSession();
	FIntPoint ComputeBrowserSize() const;
	FIntPoint ToSourcePixels(const FVector2D& Position) const;
	FText GetControllerTitle() const;
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

	// Sound through the game's Wwise (TSWwiseScreens): the screen's AkComponent and the current playing id.
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> WwiseEmitter;
	uint32 WwisePlayingId = 0;
	TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> WwiseReader;
	double WwiseStartedAt = 0.0;
	int32 WwiseQuickEnds = 0;
	// Plays Reader through Wwise; false if Wwise is off or wouldn't play it (the Unreal path is used instead).
	bool PlayThroughWwise(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& Reader);
	void StopWwise();
	void PlaceWwiseEmitter();
	UPROPERTY(Transient)
	TObjectPtr<UTSStreamingSoundWave> Wave;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> StatusTarget;
	FText StatusLine1, StatusLine2;

	TSHostProtocol::ESessionState HostState = TSHostProtocol::ESessionState::Resolving;
	bool bActive = false;
	bool bPinned = false;
	bool bBrowserSession = false;
	bool bWindowSession = false;
	bool bRemoteSession = false;
	// Where the source's picture sits in the shown texture (for mapping window positions back to source pixels).
	int32 ShownTexWidth = 0, ShownTexHeight = 0, ShownSrcX = 0, ShownSrcY = 0, ShownDstX = 0, ShownDstY = 0, ShownWidth = 0, ShownHeight = 0;

	// Live stream, controller side.
	uint32 SenderSession = 0;
	uint32 SenderSource = 0;
	uint32 NextPacketId[2] = {0, 0};
	TSharedPtr<FTSStreamSenderListener> SenderListener;
	FText StreamStats;
	double NextSenderCheck = 0.0;
	FDelegateHandle KeyframeHandle;
	struct FQueuedChunk
	{
		FTSStreamChunk Chunk;
		double QueuedAt = 0.0;
	};
	TArray<FQueuedChunk> SendQueue;
	double OldestQueuedSeconds() const;
	int32 SendQueueHead = 0;
	int64 SendQueueBytes = 0;
	double SendBudget = 0.0;
	double SendBudgetAt = 0.0;
	double NextRateRaise = 0.0;
	uint32 CurrentKbps = 0;
	uint32 WantedKbps = 0;
	double LastCongestion = 0.0;
	double NextBitrateChange = 0.0;
	void FlushStreamQueue();
	void ClearStreamQueue();
	void AdaptBitrate();
	void UpdateStreamSender();
	void OpenStreamSender();
	void CloseStreamSender();
	void OnKeyframeRequested();

	// Live stream, viewer side.
	struct FStreamPartial
	{
		bool bActive = false;
		uint32 PacketId = 0;
		uint8 Flags = 0;
		uint32 PtsMs = 0;
		uint32 Codec = 0;
		int32 Received = 0;
		TArray<TArray<uint8>> Parts;
		TArray<bool> Have;
	};
	bool bStreamReceiver = false;
	bool bWatching = false;
	FStreamPartial Partial[2];
	uint32 LastPacketId[2] = {0, 0};
	bool bHaveLast[2] = {false, false};
	double LastKeyframeRequest = 0.0;
	bool bAwaitingKeyframe = false;
	FDelegateHandle ChunkHandle;
	FDelegateHandle ViewerInputHandle;
	FDelegateHandle MarkerHandle;
	TArray<FMarker> Markers;
	bool bMarkersDirty = false;
	double LastMarkerCompose = 0.0;
	void OnViewerInput(const FTSViewerInput& Input, const FString& ViewerName, ETSInputScope Scope);
	void OnScreenMarker(const FString& Name, float X, float Y, bool bPing);
	// Sends a viewer input event to the controller through the server (pointer moves unreliably).
	void SendViewerInput(FTSViewerInput& Input, const FVector2D& Position, bool bReliable);
	// Draws the last frame into the texture, with the markers.
	void ComposeFrame();
	void ReconcileViewer(ATSBuildableScreen* Screen);
	void OpenReceiverSession(const FText& Title);
	void SetWatching(bool bInWatching);
	void OnStreamChunk(const FTSStreamChunk& Chunk);
	void RequestKeyframe(bool bForce);
	void ResetReassembly();
	UTSRemoteCallObject* FindRemoteCallObject() const;

	TWeakObjectPtr<UTSScreenPlayer> LoopbackSource;
	FText LoopbackStats;

public:
	// The loopback test's live encoder statistics, or empty.
	FText GetLoopbackStats() const { return LoopbackSource.IsValid() ? LoopbackStats : FText::GetEmpty(); }

private:
	FTSBrowserState BrowserState;
	FIntPoint BrowserSize = FIntPoint(1280, 720);
	double NextSync = 0.0;
	double NextRank = 0.0;
	double NextAudioUpdate = 0.0;
	FDelegateHandle ScreenChangedHandle;
};
