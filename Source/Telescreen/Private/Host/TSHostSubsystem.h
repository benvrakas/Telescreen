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

	TSharedPtr<FTSHostConnection> Connection;
	void* ProcessHandle = nullptr;
	void* JobHandle = nullptr;
	bool bConnected = false;
	bool bHelloSent = false;
	double NextStartAttempt = 0.0;
	int32 StartAttempts = 0;
	uint32 NextSessionId = 1;
	uint32 NextProbeId = 1;
	TMap<uint32, ITSHostSessionListener*> Listeners;
	TMap<uint32, TFunction<void(const FTSProbeResult&)>> PendingProbes;
	FText LastProblem;
};
