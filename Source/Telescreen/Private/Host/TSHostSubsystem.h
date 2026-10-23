#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Host/TSHostProtocol.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TSHostSubsystem.generated.h"

class FTSHostConnection;

// What a browser session reports about itself.
struct FTSBrowserState
{
	FString Url;
	FString Title;
	bool bLoading = false;
	bool bCanGoBack = false;
	bool bCanGoForward = false;
	// cef_cursor_type_t of the cursor the page wants.
	uint8 Cursor = 0;
	bool bPointerLocked = false;
};

// Builds the little-endian payloads of helper messages.
struct FTSPayloadWriter
{
	TArray<uint8> Bytes;

	void U8(uint8 V) { Bytes.Add(V); }
	void U16(uint16 V) { Bytes.Append(reinterpret_cast<const uint8*>(&V), 2); }
	void U32(uint32 V) { Bytes.Append(reinterpret_cast<const uint8*>(&V), 4); }
	void I32(int32 V) { Bytes.Append(reinterpret_cast<const uint8*>(&V), 4); }
	void F64(double V) { Bytes.Append(reinterpret_cast<const uint8*>(&V), 8); }
	void Str(const FString& V)
	{
		const FTCHARToUTF8 Utf8(*V);
		U32(uint32(Utf8.Length()));
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
};

// Receives one media session's messages from the helper (game thread).
// A subtitle track of a media session (kind: 0 the site's, 1 automatic captions, 2 inside the file, 3 a file next to it,
// 4 a picture track inside the file, drawn into the picture by the helper).
struct FTSSubtitleTrack
{
	uint32 Id = 0;
	FString Language;
	FString Label;
	uint8 Kind = 0;
};

struct FTSSubtitleCue
{
	uint32 StartMs = 0;
	uint32 EndMs = 0;
	FString Text;
};

class ITSHostSessionListener
{
public:
	virtual ~ITSHostSessionListener() = default;
	virtual void OnHostVideo(uint32 Generation, uint32 Width, uint32 Height, const FString& MappingName) = 0;
	virtual void OnHostAudio(uint32 SampleRate, uint32 Channels, uint32 CapacityFrames, const FString& MappingName) = 0;
	virtual void OnHostState(TSHostProtocol::ESessionState State, double Position, double Duration, const FString& Message) = 0;
	// The helper stopped; every session is gone and must be reopened.
	virtual void OnHostLost() = 0;
	// Browser sessions only.
	virtual void OnHostBrowserState(const FTSBrowserState& State) {}
	// Stream sender sessions only: an encoded packet to send to the viewers.
	virtual void OnHostStreamPacket(uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, TArray<uint8>&& Data) {}
	// Media sessions only: the subtitle tracks found, and cues of the selected track (bReplace: instead of the earlier ones).
	virtual void OnHostSubtitleTracks(const TArray<FTSSubtitleTrack>& Tracks) {}
	virtual void OnHostSubtitleCues(uint32 Track, bool bReplace, TArray<FTSSubtitleCue>&& Cues) {}
	// Stream sessions only: a direct link changed (0 trying, 1 up, 2 down, 3 the receiver lost video and needs a keyframe).
	virtual void OnHostDirectState(uint32 Link, uint8 State, const FString& Detail) {}
	// Remote desktop sessions only: how the connection is secured (Protocol empty: not encrypted), the server
	// certificate's fingerprint, and whether it differs from the pinned one (the connection was refused then).
	virtual void OnHostRemoteSecurity(const FString& Protocol, const FString& Fingerprint, bool bChanged) {}
};

// A window on this PC that can be shared (from the helper's list).
struct FTSWindowInfo
{
	uint64 Window = 0;
	uint32 ProcessId = 0;
	FString Title;
	FString Exe;
};

struct FTSProbeResult
{
	bool bOk = false;
	FString Title;
	double Duration = 0.0;
	bool bIsLive = false;
	FString Error;
};

struct FTSOpenMediaParams
{
	FString Url;
	bool bIsLocalFile = false;
	double StartSeconds = 0.0;
	bool bPaused = false;
	uint32 MaxWidth = 1280;
	uint32 MaxHeight = 720;
	// Which automatic captions are offered (e.g. "en").
	FString SubtitleLanguage;
};

// Runs TelescreenHost.exe for this game client and routes its messages (see Docs/HostProtocol.md). Not on dedicated servers.
UCLASS()
class UTSHostSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UTSHostSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }

	// True when the helper is running and connected; starts it if it isn't.
	bool EnsureConnected();

	// Why media can't play right now, or empty.
	FText GetUnavailableReason() const;

	// Opens a media session; returns its id (0 if the helper isn't connected).
	uint32 OpenMedia(const FTSOpenMediaParams& Params, ITSHostSessionListener* Listener);
	void SyncTarget(uint32 Session, double MediaSeconds, bool bPaused);

	// Opens a browser session of Width x Height pixels; returns its id (0 if the helper isn't connected).
	uint32 OpenBrowser(const FString& Url, uint32 Width, uint32 Height, bool bKeepLogins, const FString& Profile, ITSHostSessionListener* Listener);

	// Sends one input event; Body is the payload after the session id and kind (Docs/HostProtocol.md).
	void SendBrowserInput(uint32 Session, TSHostProtocol::EInput Kind, const TArray<uint8>& Body);

	// Sends one navigation command; Body is the payload after the session id and command.
	void SendBrowserCommand(uint32 Session, TSHostProtocol::ECommand Command, const TArray<uint8>& Body);

	// Live stream, controller side: encodes SourceSession (a browser session); packets arrive as OnHostStreamPacket.
	// MaxHeight: taller frames are scaled down (0 keeps the source size). Encoder: empty, "av1", "hevc", "h264" or "software".
	uint32 OpenStreamSender(uint32 SourceSession, uint32 Fps, uint32 Kbps, uint32 MaxHeight, const FString& Encoder, ITSHostSessionListener* Listener);

	// Live stream, viewer side: decodes packets passed to SendStreamPacket into this session's picture and sound.
	uint32 OpenStreamReceiver(ITSHostSessionListener* Listener);

	void SendStreamPacket(uint32 Session, uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, const TArray<uint8>& Data);

	// Command 0: keyframe, 1: bitrate (Value kbps), 2: drop video until the next keyframe.
	void SendStreamControl(uint32 Session, TSHostProtocol::EStreamControl Command, uint32 Value);

	// Shares a window of this PC (Window from ListWindows); frames larger than MaxWidth x MaxHeight are scaled down.
	uint32 OpenWindow(uint64 Window, bool bCursor, bool bAudio, uint32 MaxWidth, uint32 MaxHeight, ITSHostSessionListener* Listener);

	// Connects to a VNC server; frames larger than MaxWidth x MaxHeight are scaled down.
	// This PC's direct-link addresses (opens the helper's UDP endpoint the first time; a few seconds). OnDone gets an
	// empty list when direct links aren't available.
	void RequestDirectCandidates(TFunction<void(const TArray<FString>&)> OnDone);
	void AddDirectPeer(uint32 Session, uint32 Link, bool bSender, const TArray<uint8>& Key, const TArray<FString>& Candidates);
	void RemoveDirectPeer(uint32 Link);

	// WantWidth x WantHeight: the desktop size to ask the server for (0 x 0: none).
	// Username, Pin (the certificate fingerprint seen before) and bRequireTls: see the VeNCrypt notes in Docs/RemoteDesktop.md.
	uint32 OpenVnc(const FString& Host, uint16 Port, const FString& Password, uint32 MaxWidth, uint32 MaxHeight, bool bAudio, uint32 WantWidth, uint32 WantHeight,
		const FString& Username, const FString& Pin, bool bRequireTls, ITSHostSessionListener* Listener);

	// Asks the helper for the windows that can be shared; OnDone runs on the game thread (empty if the helper isn't
	// running).
	void ListWindows(TFunction<void(const TArray<FTSWindowInfo>&)> OnDone);

	// M3 test: shows SourceSession's frames after the stream encoder and decoder; returns the new session id.
	uint32 OpenLoopback(uint32 SourceSession, uint32 Fps, uint32 Kbps, ITSHostSessionListener* Listener);

	// True if the browser component is installed (the built-in browser can be used).
	bool IsBrowserAvailable() const;
	void CloseSession(uint32 Session);

	// Looks up a link's title and duration with yt-dlp; OnDone runs on the game thread.
	void Probe(const FString& Url, TFunction<void(const FTSProbeResult&)> OnDone);

private:
	bool StartHost();
	void StopHost(const TCHAR* Reason);
	void Dispatch(uint16 Type, const TArray<uint8>& Payload);
	void Send(TSHostProtocol::EMsg Type, const TArray<uint8>& Payload);
	// Logs a backed-up pipe and restarts a helper that stopped reading; false if it did.
	bool CheckWrites();

	TSharedPtr<FTSHostConnection> Connection;
	double LastTickAt = 0.0;
	bool bWriteStallLogged = false;
	void* ProcessHandle = nullptr;
	void* JobHandle = nullptr;
	bool bConnected = false;
	bool bHelloSent = false;
	double NextStartAttempt = 0.0;
	int32 StartAttempts = 0;
	uint32 NextSessionId = 1;
	uint32 NextProbeId = 1;
	uint32 NextWindowListId = 1;
	TMap<uint32, TFunction<void(const TArray<FTSWindowInfo>&)>> PendingWindowLists;
	TMap<uint32, ITSHostSessionListener*> Listeners;
	TMap<uint32, TFunction<void(const FTSProbeResult&)>> PendingProbes;
	TArray<TFunction<void(const TArray<FString>&)>> PendingDirect;
	TArray<FString> DirectCandidates;
	bool bDirectRequested = false;
	bool bDirectReady = false;
	FText LastProblem;
};
