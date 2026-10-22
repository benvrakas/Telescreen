#include "Host/TSHostSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Runtime/TSRuntimeComponents.h"
#include "Telescreen.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "Telescreen"

using namespace TSHostProtocol;

namespace
{
	constexpr double RestartDelaySeconds = 5.0;
	constexpr int32 MaxStartAttempts = 5;

	void PutU8(TArray<uint8>& Out, uint8 V) { Out.Add(V); }
	void PutU32(TArray<uint8>& Out, uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); }
	void PutF64(TArray<uint8>& Out, double V) { Out.Append(reinterpret_cast<const uint8*>(&V), 8); }
	void PutStr(TArray<uint8>& Out, const FString& V)
	{
		const FTCHARToUTF8 Utf8(*V);
		PutU32(Out, uint32(Utf8.Length()));
		Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	struct FPayloadReader
	{
		const TArray<uint8>& Bytes;
		int32 Pos = 0;
		bool bOk = true;

		explicit FPayloadReader(const TArray<uint8>& In) : Bytes(In) {}

		bool Take(void* Out, int32 Size)
		{
			if (!bOk || Pos + Size > Bytes.Num())
			{
				bOk = false;
				return false;
			}
			FMemory::Memcpy(Out, Bytes.GetData() + Pos, Size);
			Pos += Size;
			return true;
		}
		uint8 U8() { uint8 V = 0; Take(&V, 1); return V; }
		uint32 U32() { uint32 V = 0; Take(&V, 4); return V; }
		double F64() { double V = 0; Take(&V, 8); return V; }
		FString Str()
		{
			const uint32 Size = U32();
			if (!bOk || Pos + int32(Size) > Bytes.Num())
			{
				bOk = false;
				return FString();
			}
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Pos), Size);
			Pos += Size;
			return FString(Converted.Length(), Converted.Get());
		}
	};
}

// Client end of the helper's pipe: connects, then reads messages on its own thread into a queue for the game thread.
class FTSHostConnection : public FRunnable
{
public:
	explicit FTSHostConnection(const FString& InPipeName) : PipeName(InPipeName)
	{
		Thread = FRunnableThread::Create(this, TEXT("TelescreenHostPipe"));
	}

	virtual ~FTSHostConnection() override
	{
		Stop();
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
		}
#if PLATFORM_WINDOWS
		if (Pipe != INVALID_HANDLE_VALUE) CloseHandle(Pipe);
		if (ReadEvent) CloseHandle(ReadEvent);
		if (WriteEvent) CloseHandle(WriteEvent);
#endif
	}

	virtual uint32 Run() override
	{
#if PLATFORM_WINDOWS
		ReadEvent = CreateEventW(nullptr, true, false, nullptr);
		WriteEvent = CreateEventW(nullptr, true, false, nullptr);
		const double Deadline = FPlatformTime::Seconds() + 15.0;
		while (!bStopping && FPlatformTime::Seconds() < Deadline)
		{
			HANDLE Handle = CreateFileW(*PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (Handle != INVALID_HANDLE_VALUE)
			{
				Pipe = Handle;
				break;
			}
			FPlatformProcess::Sleep(0.1f);
		}
		if (Pipe == INVALID_HANDLE_VALUE)
		{
			UE_LOG(LogTelescreen, Error, TEXT("Host: could not connect to %s"), *PipeName);
			bClosed = true;
			return 0;
		}
		bPipeOpen = true;
		UE_LOG(LogTelescreen, Log, TEXT("Host: connected to %s"), *PipeName);
		while (!bStopping)
		{
			FMsgHeader Header{};
			if (!Transfer(false, &Header, sizeof(Header), ReadEvent) || Header.PayloadSize > MaxPayloadSize)
			{
				break;
			}
			TArray<uint8> Payload;
			Payload.SetNumUninitialized(Header.PayloadSize);
			if (Header.PayloadSize > 0 && !Transfer(false, Payload.GetData(), Header.PayloadSize, ReadEvent))
			{
				break;
			}
			Incoming.Enqueue(MakeTuple(Header.Type, MoveTemp(Payload)));
		}
		if (!bStopping)
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Host: pipe closed"));
		}
#endif
		bClosed = true;
		return 0;
	}

	virtual void Stop() override
	{
		bStopping = true;
#if PLATFORM_WINDOWS
		if (Pipe != INVALID_HANDLE_VALUE) CancelIoEx(Pipe, nullptr);
#endif
	}

	bool Write(const TArray<uint8>& Bytes)
	{
#if PLATFORM_WINDOWS
		FScopeLock Lock(&WriteLock);
		return bPipeOpen && !bClosed && Transfer(true, const_cast<uint8*>(Bytes.GetData()), Bytes.Num(), WriteEvent);
#else
		return false;
#endif
	}

	bool IsOpen() const { return bPipeOpen && !bClosed; }
	bool IsClosed() const { return bClosed; }

	TQueue<TTuple<uint16, TArray<uint8>>, EQueueMode::Spsc> Incoming;

private:
#if PLATFORM_WINDOWS
	bool Transfer(bool bWrite, void* Data, int32 Size, HANDLE Event)
	{
		uint8* Bytes = static_cast<uint8*>(Data);
		while (Size > 0)
		{
			OVERLAPPED Overlapped{};
			Overlapped.hEvent = Event;
			ResetEvent(Event);
			DWORD Done = 0;
			const BOOL bStarted = bWrite ? WriteFile(Pipe, Bytes, Size, nullptr, &Overlapped) : ReadFile(Pipe, Bytes, Size, nullptr, &Overlapped);
			if (!bStarted && GetLastError() != ERROR_IO_PENDING) return false;
			if (!GetOverlappedResult(Pipe, &Overlapped, &Done, true) || Done == 0) return false;
			Bytes += Done;
			Size -= Done;
		}
		return true;
	}

	HANDLE Pipe = INVALID_HANDLE_VALUE;
	HANDLE ReadEvent = nullptr;
	HANDLE WriteEvent = nullptr;
#endif
	FString PipeName;
	FRunnableThread* Thread = nullptr;
	FCriticalSection WriteLock;
	std::atomic<bool> bStopping{false};
	std::atomic<bool> bPipeOpen{false};
	std::atomic<bool> bClosed{false};
};

UTSHostSubsystem* UTSHostSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UTSHostSubsystem>() : nullptr;
}

bool UTSHostSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer();
}

void UTSHostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FTSRuntimeComponents::Get().StartVerification();
}

void UTSHostSubsystem::Deinitialize()
{
	StopHost(TEXT("game instance shutting down"));
	Super::Deinitialize();
}

TStatId UTSHostSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTSHostSubsystem, STATGROUP_Tickables);
}

FText UTSHostSubsystem::GetUnavailableReason() const
{
	FTSRuntimeComponents& Runtime = FTSRuntimeComponents::Get();
	if (!Runtime.IsReady() || !Runtime.IsAvailable(FTSRuntimeComponents::Host))
	{
		return Runtime.GetUnavailableReason(FTSRuntimeComponents::Host);
	}
	if (!bConnected)
	{
		return LastProblem.IsEmpty() ? LOCTEXT("HostStarting", "Starting Telescreen's player...") : LastProblem;
	}
	return FText::GetEmpty();
}

bool UTSHostSubsystem::EnsureConnected()
{
	if (bConnected)
	{
		return true;
	}
	if (!Connection.IsValid() && FPlatformTime::Seconds() >= NextStartAttempt && StartAttempts < MaxStartAttempts)
	{
		StartHost();
	}
	return false;
}

bool UTSHostSubsystem::StartHost()
{
#if PLATFORM_WINDOWS
	FTSRuntimeComponents& Runtime = FTSRuntimeComponents::Get();
	if (!Runtime.IsReady() || !Runtime.IsAvailable(FTSRuntimeComponents::Host))
	{
		return false;
	}
	++StartAttempts;
	NextStartAttempt = FPlatformTime::Seconds() + RestartDelaySeconds;

	static int32 Counter = 0;
	const uint32 GamePid = FPlatformProcess::GetCurrentProcessId();
	const FString PipeName = FString::Printf(TEXT("\\\\.\\pipe\\Telescreen-%u-%d"), GamePid, ++Counter);
	const FString Exe = Runtime.GetFilePath(FTSRuntimeComponents::Host, TEXT("TelescreenHost.exe"));
	const FString Log = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir() / TEXT("TelescreenHost.log"));
	FString CommandLine = FString::Printf(TEXT("\"%s\" --pipe \"%s\" --parent %u --log \"%s\""), *Exe, *PipeName, GamePid, *Log);
	if (Runtime.IsAvailable(FTSRuntimeComponents::Fetcher))
	{
		CommandLine += FString::Printf(TEXT(" --ytdlp \"%s\" --jsruntime \"%s\""), *Runtime.GetFilePath(FTSRuntimeComponents::Fetcher, TEXT("yt-dlp.exe")),
			*Runtime.GetFilePath(FTSRuntimeComponents::Fetcher, TEXT("qjs.exe")));
	}
	else
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Host: yt-dlp unavailable; only local files will play (%s)"),
			*Runtime.GetUnavailableReason(FTSRuntimeComponents::Fetcher).ToString());
	}
	if (Runtime.IsAvailable(FTSRuntimeComponents::Browser))
	{
		const FString BrowserData = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("Browser"));
		CommandLine += FString::Printf(TEXT(" --browserdir \"%s\" --browserdata \"%s\""),
			*FPaths::ConvertRelativePathToFull(FPaths::GetPath(Runtime.GetFilePath(FTSRuntimeComponents::Browser, TEXT("libcef.dll")))),
			*BrowserData.Replace(TEXT("/"), TEXT("\\")));
	}

	// The job object kills the helper with the game, even if the game crashes.
	HANDLE Job = CreateJobObjectW(nullptr, nullptr);
	if (Job)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
		Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits));
	}
	STARTUPINFOW StartupInfo{sizeof(StartupInfo)};
	PROCESS_INFORMATION ProcessInfo{};
	const FString WorkingDir = FPaths::GetPath(Exe);
	if (!CreateProcessW(nullptr, CommandLine.GetCharArray().GetData(), nullptr, nullptr, false, CREATE_NO_WINDOW | CREATE_SUSPENDED |
		BELOW_NORMAL_PRIORITY_CLASS, nullptr, *WorkingDir, &StartupInfo, &ProcessInfo))
	{
		const uint32 Error = GetLastError();
		UE_LOG(LogTelescreen, Error, TEXT("Host: could not start %s (error %u)"), *Exe, Error);
		LastProblem = FText::Format(LOCTEXT("HostStartFailed", "Telescreen's player could not start (error {0})."), FText::AsNumber(int32(Error)));
		if (Job) CloseHandle(Job);
		return false;
	}
	if (Job && !AssignProcessToJobObject(Job, ProcessInfo.hProcess))
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Host: could not put the helper in a job object (error %u)"), GetLastError());
	}
	ResumeThread(ProcessInfo.hThread);
	CloseHandle(ProcessInfo.hThread);
	ProcessHandle = ProcessInfo.hProcess;
	JobHandle = Job;
	UE_LOG(LogTelescreen, Log, TEXT("Host: started TelescreenHost (pid %u, attempt %d), log %s"), ProcessInfo.dwProcessId, StartAttempts, *Log);

	Connection = MakeShared<FTSHostConnection>(PipeName);
	return true;
#else
	return false;
#endif
}

void UTSHostSubsystem::StopHost(const TCHAR* Reason)
{
	if (Connection.IsValid() || ProcessHandle)
	{
		UE_LOG(LogTelescreen, Log, TEXT("Host: stopping (%s)"), Reason);
	}
	if (Connection.IsValid() && Connection->IsOpen())
	{
		Send(EMsg::Shutdown, {});
	}
	Connection.Reset();
	bConnected = false;
#if PLATFORM_WINDOWS
	if (ProcessHandle)
	{
		if (WaitForSingleObject(ProcessHandle, 500) == WAIT_TIMEOUT)
		{
			TerminateProcess(ProcessHandle, 1);
		}
		CloseHandle(ProcessHandle);
		ProcessHandle = nullptr;
	}
	if (JobHandle)
	{
		CloseHandle(JobHandle);
		JobHandle = nullptr;
	}
#endif
	TMap<uint32, ITSHostSessionListener*> Lost = MoveTemp(Listeners);
	Listeners.Reset();
	for (const auto& Pair : Lost)
	{
		Pair.Value->OnHostLost();
	}
	for (auto& Pair : PendingProbes)
	{
		FTSProbeResult Failed;
		Failed.Error = TEXT("Telescreen's player stopped");
		Pair.Value(Failed);
	}
	PendingProbes.Reset();
	for (auto& Pair : PendingWindowLists)
	{
		Pair.Value(TArray<FTSWindowInfo>());
	}
	PendingWindowLists.Reset();
}

void UTSHostSubsystem::Tick(float DeltaTime)
{
	if (!Connection.IsValid())
	{
		return;
	}
	TTuple<uint16, TArray<uint8>> Message;
	while (Connection.IsValid() && Connection->Incoming.Dequeue(Message))
	{
		Dispatch(Message.Get<0>(), Message.Get<1>());
	}
	if (!Connection.IsValid())
	{
		return;
	}
	if (Connection->IsOpen() && !bConnected && !bHelloSent)
	{
		TArray<uint8> Hello;
		PutU32(Hello, Version);
		Send(EMsg::Hello, Hello);
		bHelloSent = true;
	}
	bool bProcessGone = false;
#if PLATFORM_WINDOWS
	bProcessGone = ProcessHandle && WaitForSingleObject(ProcessHandle, 0) == WAIT_OBJECT_0;
	if (bProcessGone)
	{
		DWORD ExitCode = 0;
		GetExitCodeProcess(ProcessHandle, &ExitCode);
		UE_LOG(LogTelescreen, Warning, TEXT("Host: helper exited with code %u"), ExitCode);
		LastProblem = FText::Format(LOCTEXT("HostExited", "Telescreen's player stopped (exit code {0}); restarting."), FText::AsNumber(int32(ExitCode)));
	}
#endif
	if (bProcessGone || Connection->IsClosed())
	{
		bHelloSent = false;
		StopHost(bProcessGone ? TEXT("helper exited") : TEXT("pipe closed"));
		if (StartAttempts >= MaxStartAttempts)
		{
			UE_LOG(LogTelescreen, Error, TEXT("Host: giving up after %d attempts; see TelescreenHost.log"), StartAttempts);
			LastProblem = LOCTEXT("HostGaveUp", "Telescreen's player keeps stopping. See TelescreenHost.log in the game's log folder.");
		}
	}
}

void UTSHostSubsystem::Send(EMsg Type, const TArray<uint8>& Payload)
{
	if (!Connection.IsValid())
	{
		return;
	}
	TArray<uint8> Bytes;
	Bytes.Reserve(sizeof(FMsgHeader) + Payload.Num());
	const FMsgHeader Header{uint32(Payload.Num()), uint16(Type), 0};
	Bytes.Append(reinterpret_cast<const uint8*>(&Header), sizeof(Header));
	Bytes.Append(Payload);
	if (!Connection->Write(Bytes))
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Host: could not send message %u"), uint32(Type));
	}
}

void UTSHostSubsystem::Dispatch(uint16 Type, const TArray<uint8>& Payload)
{
	FPayloadReader R(Payload);
	switch (EMsg(Type))
	{
	case EMsg::HelloAck:
	{
		const uint32 HostVersion = R.U32();
		const FString HostBuild = R.Str();
		const uint32 HostPid = R.U32();
		if (HostVersion != Version)
		{
			UE_LOG(LogTelescreen, Error, TEXT("Host: protocol %u, mod expects %u"), HostVersion, Version);
			LastProblem = LOCTEXT("HostVersion", "Telescreen's player is a different version than the mod. Reinstall Telescreen.");
			StartAttempts = MaxStartAttempts;
			StopHost(TEXT("protocol mismatch"));
			return;
		}
		bConnected = true;
		StartAttempts = 0;
		LastProblem = FText::GetEmpty();
		UE_LOG(LogTelescreen, Log, TEXT("Host: ready (TelescreenHost %s, pid %u)"), *HostBuild, HostPid);
		break;
	}
	case EMsg::SessionVideo:
	{
		const uint32 Session = R.U32();
		const uint32 Generation = R.U32();
		const uint32 Width = R.U32();
		const uint32 Height = R.U32();
		const FString Mapping = R.Str();
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostVideo(Generation, Width, Height, Mapping);
		}
		break;
	}
	case EMsg::SessionAudio:
	{
		const uint32 Session = R.U32();
		const uint32 Rate = R.U32();
		const uint32 Channels = R.U32();
		const uint32 Capacity = R.U32();
		const FString Mapping = R.Str();
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostAudio(Rate, Channels, Capacity, Mapping);
		}
		break;
	}
	case EMsg::SessionState:
	{
		const uint32 Session = R.U32();
		const ESessionState State = ESessionState(R.U8());
		const double Position = R.F64();
		const double Duration = R.F64();
		const FString Message = R.Str();
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostState(State, Position, Duration, Message);
		}
		break;
	}
	case EMsg::StreamPacket:
	{
		const uint32 Session = R.U32();
		const uint8 Kind = R.U8();
		const uint8 Flags = R.U8();
		const uint32 PtsMs = R.U32();
		const uint32 Codec = R.U32();
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk && R.Pos <= Payload.Num())
		{
			TArray<uint8> Data;
			Data.Append(Payload.GetData() + R.Pos, Payload.Num() - R.Pos);
			(*Listener)->OnHostStreamPacket(Kind, Flags, PtsMs, Codec, MoveTemp(Data));
		}
		break;
	}
	case EMsg::BrowserState:
	{
		const uint32 Session = R.U32();
		FTSBrowserState State;
		State.Url = R.Str();
		State.Title = R.Str();
		State.bLoading = R.U8() != 0;
		State.bCanGoBack = R.U8() != 0;
		State.bCanGoForward = R.U8() != 0;
		State.Cursor = R.U8();
		State.bPointerLocked = R.U8() != 0;
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostBrowserState(State);
		}
		break;
	}
	case EMsg::DirectCandidates:
	{
		R.U32();
		const bool bOk = R.U8() != 0;
		const FString Error = R.Str();
		const uint32 Count = R.U32();
		DirectCandidates.Reset();
		for (uint32 I = 0; I < Count && R.bOk && I < 16; ++I)
		{
			DirectCandidates.Add(R.Str());
		}
		bDirectReady = true;
		UE_LOG(LogTelescreen, Log, TEXT("Host: direct links %s (%d address(es))%s%s"), bOk ? TEXT("ready") : TEXT("unavailable"), DirectCandidates.Num(),
			Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error);
		if (!bOk)
		{
			DirectCandidates.Reset();
		}
		TArray<TFunction<void(const TArray<FString>&)>> Waiting = MoveTemp(PendingDirect);
		for (auto& Callback : Waiting)
		{
			Callback(DirectCandidates);
		}
		break;
	}
	case EMsg::DirectState:
	{
		const uint32 Session = R.U32();
		const uint32 Link = R.U32();
		const uint8 State = R.U8();
		const FString Detail = R.Str();
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostDirectState(Link, State, Detail);
		}
		break;
	}
	case EMsg::SubtitleTracks:
	{
		const uint32 Session = R.U32();
		const uint32 Count = R.U32();
		TArray<FTSSubtitleTrack> Tracks;
		for (uint32 I = 0; I < Count && R.bOk && I < 200; ++I)
		{
			FTSSubtitleTrack& Track = Tracks.AddDefaulted_GetRef();
			Track.Id = R.U32();
			Track.Language = R.Str();
			Track.Label = R.Str();
			Track.Kind = R.U8();
		}
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostSubtitleTracks(Tracks);
		}
		break;
	}
	case EMsg::SubtitleCues:
	{
		const uint32 Session = R.U32();
		const uint32 Track = R.U32();
		const bool bReplace = R.U8() != 0;
		const uint32 Count = R.U32();
		TArray<FTSSubtitleCue> Cues;
		Cues.Reserve(FMath::Min<uint32>(Count, 5000));
		for (uint32 I = 0; I < Count && R.bOk && I < 5000; ++I)
		{
			FTSSubtitleCue& Cue = Cues.AddDefaulted_GetRef();
			Cue.StartMs = R.U32();
			Cue.EndMs = R.U32();
			Cue.Text = R.Str();
		}
		if (ITSHostSessionListener** Listener = Listeners.Find(Session); Listener && R.bOk)
		{
			(*Listener)->OnHostSubtitleCues(Track, bReplace, MoveTemp(Cues));
		}
		break;
	}
	case EMsg::WindowList:
	{
		const uint32 Request = R.U32();
		const uint32 Count = R.U32();
		TArray<FTSWindowInfo> Windows;
		for (uint32 I = 0; I < Count && R.bOk && I < 1000; ++I)
		{
			FTSWindowInfo& Info = Windows.AddDefaulted_GetRef();
			const uint32 Low = R.U32();
			const uint32 High = R.U32();
			Info.Window = uint64(Low) | (uint64(High) << 32);
			Info.ProcessId = R.U32();
			Info.Title = R.Str();
			Info.Exe = R.Str();
		}
		TFunction<void(const TArray<FTSWindowInfo>&)> OnDone;
		if (R.bOk && PendingWindowLists.RemoveAndCopyValue(Request, OnDone))
		{
			UE_LOG(LogTelescreen, Log, TEXT("Host: %d shareable windows"), Windows.Num());
			OnDone(Windows);
		}
		break;
	}
	case EMsg::ProbeResult:
	{
		const uint32 Request = R.U32();
		FTSProbeResult Result;
		Result.bOk = R.U8() != 0;
		Result.Title = R.Str();
		Result.Duration = R.F64();
		Result.bIsLive = R.U8() != 0;
		Result.Error = R.Str();
		TFunction<void(const FTSProbeResult&)> OnDone;
		if (R.bOk && PendingProbes.RemoveAndCopyValue(Request, OnDone))
		{
			UE_LOG(LogTelescreen, Log, TEXT("Host: probe %u %s: \"%s\" %.0f s%s %s"), Request, Result.bOk ? TEXT("ok") : TEXT("failed"), *Result.Title,
				Result.Duration, Result.bIsLive ? TEXT(" live") : TEXT(""), *Result.Error);
			OnDone(Result);
		}
		break;
	}
	default:
		UE_LOG(LogTelescreen, Warning, TEXT("Host: unexpected message %u (%d bytes)"), Type, Payload.Num());
		break;
	}
}

uint32 UTSHostSubsystem::OpenMedia(const FTSOpenMediaParams& Params, ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	TArray<uint8> Payload;
	PutU32(Payload, Session);
	PutStr(Payload, Params.Url);
	PutU8(Payload, Params.bIsLocalFile ? 1 : 0);
	PutF64(Payload, Params.StartSeconds);
	PutU8(Payload, Params.bPaused ? 1 : 0);
	PutU32(Payload, Params.MaxWidth);
	PutU32(Payload, Params.MaxHeight);
	PutStr(Payload, Params.SubtitleLanguage);
	Send(EMsg::OpenMedia, Payload);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u opens %s at %.2f s (%s, max %ux%u)"), Session, *Params.Url, Params.StartSeconds,
		Params.bPaused ? TEXT("paused") : TEXT("playing"), Params.MaxWidth, Params.MaxHeight);
	return Session;
}

void UTSHostSubsystem::SyncTarget(uint32 Session, double MediaSeconds, bool bPaused)
{
	if (!bConnected || !Listeners.Contains(Session))
	{
		return;
	}
	TArray<uint8> Payload;
	PutU32(Payload, Session);
	PutF64(Payload, MediaSeconds);
	PutU8(Payload, bPaused ? 1 : 0);
	Send(EMsg::SyncTarget, Payload);
}

bool UTSHostSubsystem::IsBrowserAvailable() const
{
	return FTSRuntimeComponents::Get().IsAvailable(FTSRuntimeComponents::Browser);
}

uint32 UTSHostSubsystem::OpenBrowser(const FString& Url, uint32 Width, uint32 Height, bool bKeepLogins, const FString& Profile,
	ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	W.Str(Url);
	W.U32(Width);
	W.U32(Height);
	W.U8(bKeepLogins ? 1 : 0);
	W.Str(Profile);
	Send(EMsg::OpenBrowser, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u opens a %ux%u browser at %s (%s)"), Session, Width, Height, *Url,
		bKeepLogins ? TEXT("kept logins") : TEXT("clean profile"));
	return Session;
}

uint32 UTSHostSubsystem::OpenStreamSender(uint32 SourceSession, uint32 Fps, uint32 Kbps, uint32 MaxHeight, const FString& Encoder, ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	W.U32(SourceSession);
	W.U32(Fps);
	W.U32(Kbps);
	W.U8(1);
	W.Str(Encoder);
	W.U32(MaxHeight);
	Send(EMsg::OpenStreamSender, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u streams session %u at %u fps, %u kbps, up to %u lines, encoder '%s'"), Session, SourceSession, Fps, Kbps, MaxHeight,
		*Encoder);
	return Session;
}

void UTSHostSubsystem::RequestDirectCandidates(TFunction<void(const TArray<FString>&)> OnDone)
{
	if (bDirectReady)
	{
		OnDone(DirectCandidates);
		return;
	}
	if (!EnsureConnected())
	{
		OnDone(TArray<FString>());
		return;
	}
	PendingDirect.Add(MoveTemp(OnDone));
	if (bDirectRequested)
	{
		return;
	}
	bDirectRequested = true;
	const int32 Port = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::DirectPortKey, 0), 0, 65535);
	const bool bUpnp = UTSConfig::GetBool(this, UTSConfig::DirectUpnpKey, true);
	TArray<uint8> Payload;
	PutU32(Payload, 1);
	PutU32(Payload, uint32(Port));
	PutU8(Payload, bUpnp ? 1 : 0);
	Send(EMsg::DirectOpen, Payload);
	UE_LOG(LogTelescreen, Log, TEXT("Host: opening direct links (port %d, UPnP %s)"), Port, bUpnp ? TEXT("on") : TEXT("off"));
}

void UTSHostSubsystem::AddDirectPeer(uint32 Session, uint32 Link, bool bSender, const TArray<uint8>& Key, const TArray<FString>& Candidates)
{
	if (!bConnected || Key.Num() != 16)
	{
		return;
	}
	TArray<uint8> Payload;
	PutU32(Payload, Session);
	PutU32(Payload, Link);
	PutU8(Payload, bSender ? 1 : 0);
	for (uint8 Byte : Key)
	{
		PutU8(Payload, Byte);
	}
	const int32 Count = FMath::Min(Candidates.Num(), 16);
	PutU32(Payload, uint32(Count));
	for (int32 I = 0; I < Count; ++I)
	{
		PutStr(Payload, Candidates[I].Left(64));
	}
	Send(EMsg::DirectAddPeer, Payload);
}

void UTSHostSubsystem::RemoveDirectPeer(uint32 Link)
{
	if (!bConnected)
	{
		return;
	}
	TArray<uint8> Payload;
	PutU32(Payload, Link);
	Send(EMsg::DirectRemovePeer, Payload);
}

uint32 UTSHostSubsystem::OpenStreamReceiver(ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	Send(EMsg::OpenStreamReceiver, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u receives a stream"), Session);
	return Session;
}

void UTSHostSubsystem::SendStreamPacket(uint32 Session, uint8 Kind, uint8 Flags, uint32 PtsMs, uint32 Codec, const TArray<uint8>& Data)
{
	if (!bConnected || !Listeners.Contains(Session))
	{
		return;
	}
	FTSPayloadWriter W;
	W.Bytes.Reserve(Data.Num() + 16);
	W.U32(Session);
	W.U8(Kind);
	W.U8(Flags);
	W.U32(PtsMs);
	W.U32(Codec);
	W.Bytes.Append(Data);
	Send(EMsg::StreamPacket, W.Bytes);
}

void UTSHostSubsystem::SendStreamControl(uint32 Session, EStreamControl Command, uint32 Value)
{
	if (!bConnected || !Listeners.Contains(Session))
	{
		return;
	}
	FTSPayloadWriter W;
	W.U32(Session);
	W.U8(uint8(Command));
	W.U32(Value);
	Send(EMsg::StreamControl, W.Bytes);
}

uint32 UTSHostSubsystem::OpenWindow(uint64 Window, bool bCursor, bool bAudio, uint32 MaxWidth, uint32 MaxHeight, ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	W.U32(uint32(Window & 0xFFFFFFFFull));
	W.U32(uint32(Window >> 32));
	W.U8(bCursor ? 1 : 0);
	W.U8(bAudio ? 1 : 0);
	W.U32(MaxWidth);
	W.U32(MaxHeight);
	Send(EMsg::OpenWindow, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u shares window %llx (cursor %d, sound %d, max %ux%u)"), Session, Window, int32(bCursor), int32(bAudio), MaxWidth,
		MaxHeight);
	return Session;
}

uint32 UTSHostSubsystem::OpenVnc(const FString& Host, uint16 Port, const FString& Password, uint32 MaxWidth, uint32 MaxHeight, bool bAudio, uint32 WantWidth,
	uint32 WantHeight, ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	W.Str(Host);
	W.U32(Port);
	W.Str(Password);
	W.U32(MaxWidth);
	W.U32(MaxHeight);
	W.U8(bAudio ? 1 : 0);
	W.U32(WantWidth);
	W.U32(WantHeight);
	Send(EMsg::OpenVnc, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u connects to VNC %s:%u (%s password)"), Session, *Host, Port, Password.IsEmpty() ? TEXT("no") : TEXT("with"));
	return Session;
}

void UTSHostSubsystem::ListWindows(TFunction<void(const TArray<FTSWindowInfo>&)> OnDone)
{
	if (!EnsureConnected())
	{
		OnDone(TArray<FTSWindowInfo>());
		return;
	}
	const uint32 Request = NextWindowListId++;
	PendingWindowLists.Add(Request, MoveTemp(OnDone));
	FTSPayloadWriter W;
	W.U32(Request);
	Send(EMsg::ListWindows, W.Bytes);
}

uint32 UTSHostSubsystem::OpenLoopback(uint32 SourceSession, uint32 Fps, uint32 Kbps, ITSHostSessionListener* Listener)
{
	if (!EnsureConnected())
	{
		return 0;
	}
	const uint32 Session = NextSessionId++;
	Listeners.Add(Session, Listener);
	FTSPayloadWriter W;
	W.U32(Session);
	W.U32(SourceSession);
	W.U32(Fps);
	W.U32(Kbps);
	W.Str(FString());
	Send(EMsg::OpenLoopback, W.Bytes);
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u loops back session %u at %u fps, %u kbps"), Session, SourceSession, Fps, Kbps);
	return Session;
}

void UTSHostSubsystem::SendBrowserInput(uint32 Session, EInput Kind, const TArray<uint8>& Body)
{
	if (!bConnected || !Listeners.Contains(Session))
	{
		return;
	}
	FTSPayloadWriter W;
	W.U32(Session);
	W.U8(uint8(Kind));
	W.Bytes.Append(Body);
	Send(EMsg::BrowserInput, W.Bytes);
}

void UTSHostSubsystem::SendBrowserCommand(uint32 Session, ECommand Command, const TArray<uint8>& Body)
{
	if (!bConnected || !Listeners.Contains(Session))
	{
		return;
	}
	FTSPayloadWriter W;
	W.U32(Session);
	W.U8(uint8(Command));
	W.Bytes.Append(Body);
	Send(EMsg::BrowserCommand, W.Bytes);
}

void UTSHostSubsystem::CloseSession(uint32 Session)
{
	if (Listeners.Remove(Session) == 0)
	{
		return;
	}
	UE_LOG(LogTelescreen, Log, TEXT("Host: session %u closed"), Session);
	if (bConnected)
	{
		TArray<uint8> Payload;
		PutU32(Payload, Session);
		Send(EMsg::CloseSession, Payload);
	}
}

void UTSHostSubsystem::Probe(const FString& Url, TFunction<void(const FTSProbeResult&)> OnDone)
{
	if (!EnsureConnected())
	{
		FTSProbeResult Failed;
		Failed.Error = GetUnavailableReason().ToString();
		OnDone(Failed);
		return;
	}
	const uint32 Request = NextProbeId++;
	PendingProbes.Add(Request, MoveTemp(OnDone));
	TArray<uint8> Payload;
	PutU32(Payload, Request);
	PutStr(Payload, Url);
	Send(EMsg::Probe, Payload);
}

#undef LOCTEXT_NAMESPACE
