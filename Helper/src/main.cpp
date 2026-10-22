// TelescreenHost: Telescreen's media helper process. See Docs/HostProtocol.md.

#include "Common.h"
#include "MediaSession.h"
#include "Pipe.h"
#include "YtDlp.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <mmsystem.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

using namespace TSHostProtocol;

namespace
{
	struct FArgs
	{
		std::wstring Pipe, Log, YtDlp, JsRuntime, Out;
		DWORD Parent = 0;
		std::string SelfTest;
		bool SelfTestLocal = false;
		double Seconds = 12.0, Start = 0.0, SeekAt = -1.0, SeekTo = 0.0, PauseAt = -1.0, ResumeAt = -1.0;
	};

	FArgs ParseArgs(int Argc, wchar_t** Argv)
	{
		FArgs Args;
		for (int I = 1; I + 1 < Argc; I += 2)
		{
			const std::wstring Key = Argv[I], Value = Argv[I + 1];
			if (Key == L"--pipe") Args.Pipe = Value;
			else if (Key == L"--log") Args.Log = Value;
			else if (Key == L"--ytdlp") Args.YtDlp = Value;
			else if (Key == L"--jsruntime") Args.JsRuntime = Value;
			else if (Key == L"--parent") Args.Parent = wcstoul(Value.c_str(), nullptr, 10);
			else if (Key == L"--selftest") Args.SelfTest = Narrow(Value);
			else if (Key == L"--selftest-file") { Args.SelfTest = Narrow(Value); Args.SelfTestLocal = true; }
			else if (Key == L"--seconds") Args.Seconds = _wtof(Value.c_str());
			else if (Key == L"--start") Args.Start = _wtof(Value.c_str());
			else if (Key == L"--seek-at") Args.SeekAt = _wtof(Value.c_str());
			else if (Key == L"--seek-to") Args.SeekTo = _wtof(Value.c_str());
			else if (Key == L"--pause-at") Args.PauseAt = _wtof(Value.c_str());
			else if (Key == L"--resume-at") Args.ResumeAt = _wtof(Value.c_str());
			else if (Key == L"--out") Args.Out = Value;
		}
		return Args;
	}

	void FFmpegLog(void*, int Level, const char* Format, va_list Args)
	{
		if (Level > AV_LOG_WARNING) return;
		char Line[1024];
		vsnprintf(Line, sizeof(Line), Format, Args);
		size_t Length = strlen(Line);
		while (Length > 0 && (Line[Length - 1] == '\n' || Line[Length - 1] == '\r')) Line[--Length] = 0;
		if (Length > 0) LogLine(Level <= AV_LOG_ERROR ? "FFERR" : "FFWRN", "%s", Line);
	}

	// Exits the process when the game does.
	void WatchParent(DWORD Pid)
	{
		HANDLE Process = OpenProcess(SYNCHRONIZE, FALSE, Pid);
		if (!Process)
		{
			LOG_WARN("cannot watch parent process %lu (%lu)", Pid, GetLastError());
			return;
		}
		std::thread([Process]
		{
			WaitForSingleObject(Process, INFINITE);
			LOG_INFO("game process exited; shutting down");
			ExitProcess(0);
		}).detach();
	}

	int RunHost(const FArgs& Args)
	{
		FPipeServer Pipe;
		if (!Pipe.Create(Args.Pipe) || !Pipe.WaitForClient(30000)) return 2;
		LOG_INFO("mod connected on %s", Narrow(Args.Pipe).c_str());

		std::mutex SessionsMutex;
		std::map<uint32_t, std::unique_ptr<FMediaSession>> Sessions;
		auto Retire = [](std::unique_ptr<FMediaSession> Session)
		{
			// Closing can wait on yt-dlp or the network; do it off the pipe thread.
			if (Session) std::thread([S = std::move(Session)]() mutable { S.reset(); }).detach();
		};

		FSessionEvents Events;
		Events.OnVideo = [&Pipe](uint32_t Session, uint32_t Generation, uint32_t Width, uint32_t Height, const std::string& Mapping)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.U32(Generation);
			W.U32(Width);
			W.U32(Height);
			W.Str(Mapping);
			Pipe.Send(uint16_t(EMsg::SessionVideo), W.Bytes);
		};
		Events.OnAudio = [&Pipe](uint32_t Session, uint32_t Rate, uint32_t Channels, uint32_t Capacity, const std::string& Mapping)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.U32(Rate);
			W.U32(Channels);
			W.U32(Capacity);
			W.Str(Mapping);
			Pipe.Send(uint16_t(EMsg::SessionAudio), W.Bytes);
		};
		Events.OnState = [&Pipe](uint32_t Session, ESessionState State, double Position, double Duration, const std::string& Message)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.U8(uint8_t(State));
			W.F64(Position);
			W.F64(Duration);
			W.Str(Message);
			Pipe.Send(uint16_t(EMsg::SessionState), W.Bytes);
		};

		uint16_t Type = 0;
		std::vector<uint8_t> Payload;
		while (Pipe.Read(Type, Payload))
		{
			FPayloadReader R(Payload);
			switch (EMsg(Type))
			{
			case EMsg::Hello:
			{
				const uint32_t ModVersion = R.U32();
				LOG_INFO("hello from mod (protocol %u, ours %u)", ModVersion, Version);
				FPayloadWriter W;
				W.U32(Version);
				W.Str(HOST_VERSION);
				W.U32(GetCurrentProcessId());
				Pipe.Send(uint16_t(EMsg::HelloAck), W.Bytes);
				break;
			}
			case EMsg::OpenMedia:
			{
				FOpenParams Params;
				Params.SessionId = R.U32();
				Params.Url = R.Str();
				Params.IsLocalFile = R.U8() != 0;
				Params.StartSeconds = R.F64();
				Params.Paused = R.U8() != 0;
				Params.MaxWidth = std::max(16u, R.U32());
				Params.MaxHeight = std::max(16u, R.U32());
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FMediaSession>(Params, Args.YtDlp, Events);
				break;
			}
			case EMsg::SyncTarget:
			{
				const uint32_t Session = R.U32();
				const double Media = R.F64();
				const bool Paused = R.U8() != 0;
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It != Sessions.end()) It->second->SetTarget(Media, Paused);
				break;
			}
			case EMsg::CloseSession:
			{
				const uint32_t Session = R.U32();
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It != Sessions.end())
				{
					Retire(std::move(It->second));
					Sessions.erase(It);
				}
				break;
			}
			case EMsg::Probe:
			{
				const uint32_t Request = R.U32();
				const std::string Url = R.Str();
				if (!R.Ok) break;
				std::thread([&Pipe, Request, Url, YtDlp = Args.YtDlp]
				{
					const FResolvedMedia Result = ResolveWithYtDlp(YtDlp, Url, false, 0);
					FPayloadWriter W;
					W.U32(Request);
					W.U8(Result.Ok ? 1 : 0);
					W.Str(Result.Title);
					W.F64(Result.Duration);
					W.U8(Result.IsLive ? 1 : 0);
					W.Str(Result.Error);
					Pipe.Send(uint16_t(EMsg::ProbeResult), W.Bytes);
				}).detach();
				break;
			}
			case EMsg::Shutdown:
				LOG_INFO("shutdown requested");
				ExitProcess(0);
			default:
				LOG_WARN("unknown message type %u (%zu bytes)", Type, Payload.size());
				break;
			}
		}
		LOG_INFO("pipe closed; exiting");
		ExitProcess(0);
	}

	// Plays Args.SelfTest while acting as the mod's side of the shared buffers, then reports and saves the last frame.
	int RunSelfTest(const FArgs& Args)
	{
		struct FState
		{
			std::mutex Mutex;
			HANDLE VideoMapping = nullptr, AudioMapping = nullptr;
			FVideoHeader* Video = nullptr;
			FAudioHeader* Audio = nullptr;
			std::atomic<bool> Stop{false};
			std::atomic<int64_t> AudioFrames{0};
			std::atomic<double> SumSquares{0.0};
			ESessionState LastState = ESessionState::Resolving;
			double LastPosition = 0.0;
			std::string LastMessage;
		} State;

		FSessionEvents Events;
		Events.OnVideo = [&State](uint32_t, uint32_t Generation, uint32_t Width, uint32_t Height, const std::string& Name)
		{
			std::lock_guard<std::mutex> Lock(State.Mutex);
			HANDLE Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, Widen(Name).c_str());
			State.VideoMapping = Mapping;
			State.Video = Mapping ? static_cast<FVideoHeader*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
			LOG_INFO("selftest: video generation %u %ux%u mapped=%d", Generation, Width, Height, State.Video != nullptr);
		};
		std::thread AudioConsumer;
		Events.OnAudio = [&State, &AudioConsumer](uint32_t, uint32_t Rate, uint32_t Channels, uint32_t Capacity, const std::string& Name)
		{
			HANDLE Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, Widen(Name).c_str());
			State.Audio = Mapping ? static_cast<FAudioHeader*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
			LOG_INFO("selftest: audio %u Hz x%u, %u frames, mapped=%d", Rate, Channels, Capacity, State.Audio != nullptr);
			if (!State.Audio) return;
			AudioConsumer = std::thread([&State, Rate, Channels, Capacity]
			{
				// Pulls 10 ms of audio every 10 ms, like the game's mixer.
				const float* Samples = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(State.Audio) + AudioDataOffset);
				const double Start = NowSeconds();
				int64_t Pulled = 0;
				while (!State.Stop)
				{
					const int64_t Due = int64_t((NowSeconds() - Start) * Rate);
					if (Due - Pulled < Rate / 100)
					{
						Sleep(2);
						continue;
					}
					const int64_t Want = Due - Pulled;
					int64_t Read = InterlockedCompareExchange64(&State.Audio->ReadFrames, 0, 0);
					const int64_t Discard = InterlockedCompareExchange64(&State.Audio->DiscardUntil, 0, 0);
					if (Read < Discard) Read = Discard;
					const int64_t Write = InterlockedCompareExchange64(&State.Audio->WriteFrames, 0, 0);
					const int64_t Take = std::min(Want, Write - Read);
					double Sum = 0.0;
					for (int64_t F = 0; F < Take; ++F)
					{
						const float* Frame = Samples + size_t((Read + F) % Capacity) * Channels;
						for (uint32_t C = 0; C < Channels; ++C) Sum += double(Frame[C]) * Frame[C];
					}
					InterlockedExchange64(&State.Audio->ReadFrames, Read + std::max<int64_t>(Take, 0));
					State.AudioFrames += std::max<int64_t>(Take, 0);
					State.SumSquares = State.SumSquares.load() + Sum;
					Pulled += Want;
				}
			});
		};
		Events.OnState = [&State](uint32_t, ESessionState S, double Position, double Duration, const std::string& Message)
		{
			std::lock_guard<std::mutex> Lock(State.Mutex);
			if (S != State.LastState) LOG_INFO("selftest: state %d at %.2f / %.0f %s", int(S), Position, Duration, Message.c_str());
			State.LastState = S;
			State.LastPosition = Position;
			State.LastMessage = Message;
		};

		FOpenParams Params;
		Params.SessionId = 1;
		Params.Url = Args.SelfTest;
		Params.IsLocalFile = Args.SelfTestLocal;
		Params.StartSeconds = Args.Start;
		auto Session = std::make_unique<FMediaSession>(Params, Args.YtDlp, Events);

		const double Start = NowSeconds();
		double TargetBase = Args.Start, TargetBaseTime = Start;
		bool Paused = false, Seeked = false, PausedOnce = false, Resumed = false;
		int32_t Front = 2;
		uint64_t FramesSeen = 0, LastSequence = 0;
		double NextReport = Start + 1.0;
		while (NowSeconds() - Start < Args.Seconds)
		{
			const double Elapsed = NowSeconds() - Start;
			if (Args.SeekAt >= 0 && !Seeked && Elapsed >= Args.SeekAt)
			{
				Seeked = true;
				TargetBase = Args.SeekTo;
				TargetBaseTime = NowSeconds();
				LOG_INFO("selftest: moving target to %.2f", Args.SeekTo);
			}
			if (Args.PauseAt >= 0 && !PausedOnce && Elapsed >= Args.PauseAt)
			{
				PausedOnce = Paused = true;
				TargetBase += NowSeconds() - TargetBaseTime;
				TargetBaseTime = NowSeconds();
				LOG_INFO("selftest: pausing at target %.2f", TargetBase);
			}
			if (Args.ResumeAt >= 0 && PausedOnce && !Resumed && Elapsed >= Args.ResumeAt)
			{
				Resumed = true;
				Paused = false;
				TargetBaseTime = NowSeconds();
				LOG_INFO("selftest: resuming at target %.2f", TargetBase);
			}
			const double Target = TargetBase + (Paused ? 0.0 : NowSeconds() - TargetBaseTime);
			Session->SetTarget(Target, Paused);

			{
				std::lock_guard<std::mutex> Lock(State.Mutex);
				if (State.Video && (State.Video->State & TripleFresh))
				{
					Front = InterlockedExchange(reinterpret_cast<volatile LONG*>(&State.Video->State), Front) & TripleSlotMask;
					const uint64_t Sequence = State.Video->SlotSequence[Front];
					if (Sequence != LastSequence)
					{
						++FramesSeen;
						LastSequence = Sequence;
					}
				}
			}
			if (NowSeconds() >= NextReport)
			{
				NextReport += 1.0;
				std::lock_guard<std::mutex> Lock(State.Mutex);
				const double Pts = State.Video ? State.Video->SlotPts[Front] : 0.0;
				LOG_INFO("selftest t=%.1f target=%.2f position=%.2f framePts=%.2f frames=%llu audio=%.2fs", Elapsed, Target, State.LastPosition,
					Pts, (unsigned long long)FramesSeen, State.AudioFrames.load() / 48000.0);
			}
			Sleep(4);
		}

		State.Stop = true;
		if (AudioConsumer.joinable()) AudioConsumer.join();
		const double Seconds = NowSeconds() - Start;
		const double Rms = State.AudioFrames > 0 ? std::sqrt(State.SumSquares.load() / (State.AudioFrames.load() * 2.0)) : 0.0;
		LOG_INFO("selftest result: %llu frames in %.1f s (%.1f fps), %.2f s audio consumed, audio RMS %.4f, published %llu, last state %d",
			(unsigned long long)FramesSeen, Seconds, FramesSeen / Seconds, State.AudioFrames.load() / 48000.0, Rms,
			(unsigned long long)Session->GetFramesPublished(), int(State.LastState));

		if (!Args.Out.empty() && State.Video)
		{
			const FVideoHeader& Header = *State.Video;
			const uint8_t* Pixels = reinterpret_cast<const uint8_t*>(State.Video) + VideoDataOffset + size_t(Header.SlotBytes) * Front;
			FILE* File = nullptr;
			if (_wfopen_s(&File, Args.Out.c_str(), L"wb") == 0 && File)
			{
				BITMAPFILEHEADER Fh{};
				BITMAPINFOHEADER Ih{};
				Ih.biSize = sizeof(Ih);
				Ih.biWidth = LONG(Header.Width);
				Ih.biHeight = -LONG(Header.Height);
				Ih.biPlanes = 1;
				Ih.biBitCount = 32;
				Fh.bfType = 0x4D42;
				Fh.bfOffBits = sizeof(Fh) + sizeof(Ih);
				Fh.bfSize = Fh.bfOffBits + Header.SlotBytes;
				fwrite(&Fh, sizeof(Fh), 1, File);
				fwrite(&Ih, sizeof(Ih), 1, File);
				fwrite(Pixels, Header.SlotBytes, 1, File);
				fclose(File);
				LOG_INFO("selftest: saved last frame to %s", Narrow(Args.Out).c_str());
			}
		}
		Session.reset();
		return FramesSeen > 0 || State.AudioFrames > 0 ? 0 : 1;
	}
}

int wmain(int Argc, wchar_t** Argv)
{
	const FArgs Args = ParseArgs(Argc, Argv);
	LogOpen(Args.Log);
	LOG_INFO("TelescreenHost %s starting (pid %lu, protocol %u)", HOST_VERSION, GetCurrentProcessId(), Version);
	timeBeginPeriod(1);
	av_log_set_callback(FFmpegLog);
	avformat_network_init();
	if (Args.Parent) WatchParent(Args.Parent);
	SetYtDlpJsRuntime(Args.JsRuntime);

	if (!Args.SelfTest.empty()) return RunSelfTest(Args);
	if (Args.Pipe.empty())
	{
		LOG_ERROR("usage: TelescreenHost --pipe <name> [--parent <pid>] [--log <file>] [--ytdlp <path>]  |  --selftest <url> ...");
		return 1;
	}
	return RunHost(Args);
}
