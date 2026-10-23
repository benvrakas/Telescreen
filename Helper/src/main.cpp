// TelescreenHost: Telescreen's media helper process. See Docs/HostProtocol.md.

#include "BrowserSession.h"
#include "Common.h"
#include "EncodeBench.h"
#include "LoopbackSession.h"
#include "StreamSession.h"
#include "MediaSession.h"
#include "Direct.h"
#include "Pipe.h"
#include "Taps.h"
#include "VncSession.h"
#include "WindowSession.h"
#include "YtDlp.h"

#include <algorithm>
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
		std::wstring Pipe, Log, YtDlp, JsRuntime, Out, BrowserDir, BrowserData;
		DWORD Parent = 0;
		std::string SelfTest;
		bool SelfTestLocal = false;
		bool SelfTestBrowser = false;
		bool SelfTestStream = false;
		bool SelfTestWindow = false;
		bool SelfTestVnc = false;
		bool SelfTestDirect = false;
		std::string VncPassword;
		bool VncRequireTls = false;
		std::string VncPin;
		double LossPercent = 0.0;
		int StreamHeight = 0;
		int VncWantWidth = 0, VncWantHeight = 0;
		int SubtitleTrack = 0;
		std::string SubtitleLanguage = "en";
		FEncodeBenchArgs Bench;
		bool bBench = false;
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
			else if (Key == L"--stream-height") Args.StreamHeight = _wtoi(Value.c_str());
			else if (Key == L"--subtitles") Args.SubtitleTrack = _wtoi(Value.c_str());
			else if (Key == L"--subtitle-language") Args.SubtitleLanguage = Narrow(Value);
			else if (Key == L"--vnc-require-tls") Args.VncRequireTls = Value == L"1";
			else if (Key == L"--vnc-pin") Args.VncPin = Narrow(Value);
			else if (Key == L"--vnc-size") swscanf_s(Value.c_str(), L"%dx%d", &Args.VncWantWidth, &Args.VncWantHeight);
			else if (Key == L"--jsruntime") Args.JsRuntime = Value;
			else if (Key == L"--browserdir") Args.BrowserDir = Value;
			else if (Key == L"--browserdata") Args.BrowserData = Value;
			else if (Key == L"--selftest-browser") { Args.SelfTest = Narrow(Value); Args.SelfTestBrowser = true; }
			else if (Key == L"--selftest-stream") { Args.SelfTest = Narrow(Value); Args.SelfTestStream = true; }
			else if (Key == L"--selftest-direct") { Args.SelfTestDirect = true; Args.Seconds = _wtof(Value.c_str()); }
			else if (Key == L"--selftest-stream-file") { Args.SelfTest = Narrow(Value); Args.SelfTestStream = true; Args.SelfTestLocal = true; }
			else if (Key == L"--selftest-window") { Args.SelfTest = Narrow(Value); Args.SelfTestWindow = true; }
			else if (Key == L"--selftest-vnc") { Args.SelfTest = Narrow(Value); Args.SelfTestVnc = true; }
			else if (Key == L"--password") Args.VncPassword = Narrow(Value);
			else if (Key == L"--loss") Args.LossPercent = _wtof(Value.c_str());
			else if (Key == L"--parent") Args.Parent = wcstoul(Value.c_str(), nullptr, 10);
			else if (Key == L"--selftest") Args.SelfTest = Narrow(Value);
			else if (Key == L"--selftest-file") { Args.SelfTest = Narrow(Value); Args.SelfTestLocal = true; }
			else if (Key == L"--seconds") Args.Seconds = _wtof(Value.c_str());
			else if (Key == L"--start") Args.Start = _wtof(Value.c_str());
			else if (Key == L"--seek-at") Args.SeekAt = _wtof(Value.c_str());
			else if (Key == L"--seek-to") Args.SeekTo = _wtof(Value.c_str());
			else if (Key == L"--pause-at") Args.PauseAt = _wtof(Value.c_str());
			else if (Key == L"--resume-at") Args.ResumeAt = _wtof(Value.c_str());
			else if (Key == L"--out") { Args.Out = Value; Args.Bench.Out = Value; }
			else if (Key == L"--selftest-encode") { Args.Bench.Input = Narrow(Value); Args.bBench = true; }
			else if (Key == L"--width") Args.Bench.Settings.Width = _wtoi(Value.c_str());
			else if (Key == L"--height") Args.Bench.Settings.Height = _wtoi(Value.c_str());
			else if (Key == L"--fps") Args.Bench.Settings.Fps = _wtoi(Value.c_str());
			else if (Key == L"--kbps") Args.Bench.Settings.BitrateKbps = _wtoi(Value.c_str());
			else if (Key == L"--frames") Args.Bench.Frames = _wtoi(Value.c_str());
			else if (Key == L"--prefer") Args.Bench.Settings.Prefer = Narrow(Value);
			else if (Key == L"--software") Args.Bench.Settings.bAllowSoftware = Value == L"1";
		}
		return Args;
	}

	std::wstring LogDirectory(const std::wstring& LogFile)
	{
		const size_t Slash = LogFile.find_last_of(L"\\/");
		if (Slash != std::wstring::npos) return LogFile.substr(0, Slash);
		wchar_t Temp[MAX_PATH];
		GetTempPathW(MAX_PATH, Temp);
		return Temp;
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
		std::map<uint32_t, std::unique_ptr<FSession>> Sessions;
		std::atomic<int> Retiring{0};
		auto Retire = [&Retiring](std::unique_ptr<FSession> Session)
		{
			// Closing can wait on yt-dlp, the network or the browser; do it off the pipe thread.
			if (!Session) return;
			++Retiring;
			std::thread([S = std::move(Session), &Retiring]() mutable
			{
				S.reset();
				--Retiring;
			}).detach();
		};
		auto ShutdownAll = [&]
		{
			{
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				for (auto& Pair : Sessions) Retire(std::move(Pair.second));
				Sessions.clear();
			}
			for (int I = 0; I < 100 && Retiring.load() > 0; ++I) Sleep(50);
			Browser::Shutdown();
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

		Events.OnSubtitleTracks = [&Pipe](uint32_t Session, const std::vector<FSubtitleTrack>& Tracks)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.U32(uint32_t(Tracks.size()));
			for (const FSubtitleTrack& Track : Tracks)
			{
				W.U32(Track.Id);
				W.Str(Track.Language);
				W.Str(Track.Label);
				W.U8(uint8_t(Track.Kind));
			}
			Pipe.Send(uint16_t(EMsg::SubtitleTracks), W.Bytes);
		};
		Events.OnSubtitleCues = [&Pipe](uint32_t Session, uint32_t Track, bool Replace, const std::vector<FSubtitleCue>& Cues)
		{
			// Batches stay well under the pipe's message limit.
			constexpr size_t Batch = 400;
			size_t Sent = 0;
			do
			{
				const size_t Count = std::min(Batch, Cues.size() - Sent);
				FPayloadWriter W;
				W.U32(Session);
				W.U32(Track);
				W.U8(Replace && Sent == 0 ? 1 : 0);
				W.U32(uint32_t(Count));
				for (size_t I = Sent; I < Sent + Count; ++I)
				{
					W.U32(Cues[I].StartMs);
					W.U32(Cues[I].EndMs);
					W.Str(Cues[I].Text.substr(0, 1000));
				}
				Pipe.Send(uint16_t(EMsg::SubtitleCues), W.Bytes);
				Sent += Count;
			} while (Sent < Cues.size());
		};

		Events.OnRemoteSecurity = [&Pipe](uint32_t Session, const std::string& Protocol, const std::string& Fingerprint, bool Changed)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.Str(Protocol);
			W.Str(Fingerprint);
			W.U8(Changed ? 1 : 0);
			Pipe.Send(uint16_t(EMsg::RemoteSecurity), W.Bytes);
		};

		Events.OnBrowserState = [&Pipe](uint32_t Session, const std::string& Url, const std::string& Title, bool Loading, bool CanGoBack,
									  bool CanGoForward, uint8_t Cursor, bool PointerLocked)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.Str(Url);
			W.Str(Title);
			W.U8(Loading ? 1 : 0);
			W.U8(CanGoBack ? 1 : 0);
			W.U8(CanGoForward ? 1 : 0);
			W.U8(Cursor);
			W.U8(PointerLocked ? 1 : 0);
			Pipe.Send(uint16_t(EMsg::BrowserState), W.Bytes);
		};

		// Direct links: received packets go to their receiver session; link changes go to the mod.
		FDirectHub Direct([&SessionsMutex, &Sessions](uint32_t Session, const FStreamPacket& Packet)
		{
			std::lock_guard<std::mutex> Lock(SessionsMutex);
			auto It = Sessions.find(Session);
			if (It != Sessions.end()) It->second->HandleStreamPacket(Packet);
		}, [&Pipe](uint32_t Session, uint32_t Link, FDirectHub::EState State, const std::string& Detail)
		{
			FPayloadWriter W;
			W.U32(Session);
			W.U32(Link);
			W.U8(uint8_t(State));
			W.Str(Detail);
			Pipe.Send(uint16_t(EMsg::DirectState), W.Bytes);
		});

		auto SendStreamPacket = [&Pipe, &Direct](uint32_t Session, const FStreamPacket& Packet)
		{
			Direct.Send(Session, Packet);
			FPayloadWriter W;
			W.Bytes.reserve(Packet.Data.size() + 16);
			W.U32(Session);
			W.U8(uint8_t(Packet.Kind));
			W.U8(Packet.Flags);
			W.U32(Packet.PtsMs);
			W.U32(Packet.Codec);
			W.Append(Packet.Data.data(), Packet.Data.size());
			Pipe.Send(uint16_t(EMsg::StreamPacket), W.Bytes);
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
				Params.SubtitleLanguage = R.Str();
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
				Direct.RemoveSession(Session);
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It != Sessions.end())
				{
					Retire(std::move(It->second));
					Sessions.erase(It);
				}
				break;
			}
			case EMsg::DirectOpen:
			{
				const uint32_t Request = R.U32();
				const uint16_t Port = uint16_t(R.U32());
				const bool bUpnp = R.U8() != 0;
				if (!R.Ok) break;
				// STUN and UPnP take a few seconds: off the pipe thread.
				std::thread([&Pipe, &Direct, Request, Port, bUpnp]
				{
					std::vector<std::string> Candidates;
					std::string Error;
					const bool bOk = Direct.Open(Port, bUpnp, Candidates, Error);
					FPayloadWriter W;
					W.U32(Request);
					W.U8(bOk ? 1 : 0);
					W.Str(Error);
					W.U32(uint32_t(Candidates.size()));
					for (const std::string& Candidate : Candidates) W.Str(Candidate);
					Pipe.Send(uint16_t(EMsg::DirectCandidates), W.Bytes);
				}).detach();
				break;
			}
			case EMsg::DirectAddPeer:
			{
				const uint32_t Session = R.U32();
				const uint32_t Link = R.U32();
				const bool bSender = R.U8() != 0;
				uint8_t Key[16] = {};
				for (uint8_t& Byte : Key) Byte = R.U8();
				const uint32_t Count = std::min<uint32_t>(R.U32(), 16);
				std::vector<std::string> Candidates;
				for (uint32_t I = 0; I < Count && R.Ok; ++I) Candidates.push_back(R.Str());
				if (!R.Ok) break;
				Direct.AddPeer(Session, Link, bSender, Key, Candidates);
				break;
			}
			case EMsg::DirectRemovePeer:
			{
				const uint32_t Link = R.U32();
				if (R.Ok) Direct.RemovePeer(Link);
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
			case EMsg::OpenBrowser:
			{
				FBrowserParams Params;
				Params.SessionId = R.U32();
				Params.Url = R.Str();
				Params.Width = R.U32();
				Params.Height = R.U32();
				Params.KeepLogins = R.U8() != 0;
				Params.ProfileName = R.Str();
				if (!R.Ok) break;
				std::string Error;
				if (!Browser::EnsureInitialized(Args.BrowserDir, Args.BrowserData, LogDirectory(Args.Log), Error))
				{
					LOG_ERROR("session %u: %s", Params.SessionId, Error.c_str());
					if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Error, 0.0, 0.0, Error);
					break;
				}
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FBrowserSession>(Params, Events);
				break;
			}
			case EMsg::OpenWindow:
			{
				FWindowParams Params;
				Params.SessionId = R.U32();
				const uint32_t Low = R.U32();
				const uint32_t High = R.U32();
				Params.Window = uint64_t(Low) | (uint64_t(High) << 32);
				Params.bCursor = R.U8() != 0;
				Params.bAudio = R.U8() != 0;
				Params.MaxWidth = std::clamp<uint32_t>(R.U32(), 320, 3840);
				Params.MaxHeight = std::clamp<uint32_t>(R.U32(), 180, 2160);
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FWindowSession>(Params, Events);
				break;
			}
			case EMsg::OpenVnc:
			{
				FVncParams Params;
				Params.SessionId = R.U32();
				Params.Host = R.Str();
				Params.Port = uint16_t(std::clamp<uint32_t>(R.U32(), 1, 65535));
				Params.Password = R.Str();
				Params.MaxWidth = std::clamp<uint32_t>(R.U32(), 320, 3840);
				Params.MaxHeight = std::clamp<uint32_t>(R.U32(), 180, 2160);
				Params.bAudio = R.U8() != 0;
				Params.WantWidth = std::min<uint32_t>(R.U32(), 8192);
				Params.WantHeight = std::min<uint32_t>(R.U32(), 8192);
				Params.Username = R.Str();
				Params.PinnedFingerprint = R.Str();
				Params.bRequireTls = R.U8() != 0;
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FVncSession>(Params, Events);
				break;
			}
			case EMsg::ListWindows:
			{
				const uint32_t Request = R.U32();
				if (!R.Ok) break;
				const std::vector<FWindowInfo> Windows = ListShareableWindows(Args.Parent);
				FPayloadWriter W;
				W.U32(Request);
				W.U32(uint32_t(Windows.size()));
				for (const FWindowInfo& Info : Windows)
				{
					W.U32(uint32_t(Info.Window & 0xFFFFFFFFu));
					W.U32(uint32_t(Info.Window >> 32));
					W.U32(Info.ProcessId);
					W.Str(Info.Title);
					W.Str(Info.Exe);
				}
				LOG_INFO("listed %zu shareable windows", Windows.size());
				Pipe.Send(uint16_t(EMsg::WindowList), W.Bytes);
				break;
			}
			case EMsg::OpenLoopback:
			{
				FLoopbackParams Params;
				Params.SessionId = R.U32();
				Params.SourceSessionId = R.U32();
				Params.Fps = int(std::clamp<uint32_t>(R.U32(), 1, 120));
				Params.BitrateKbps = int(std::clamp<uint32_t>(R.U32(), 100, 100000));
				Params.Prefer = R.Str();
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FLoopbackSession>(Params, Events);
				break;
			}
			case EMsg::OpenStreamSender:
			{
				FSenderParams Params;
				Params.SessionId = R.U32();
				Params.SourceSessionId = R.U32();
				Params.Fps = int(std::clamp<uint32_t>(R.U32(), 1, 120));
				Params.BitrateKbps = int(std::clamp<uint32_t>(R.U32(), 200, 50000));
				Params.bAllowSoftware = R.U8() != 0;
				Params.Prefer = R.Str();
				Params.MaxHeight = int(std::clamp<uint32_t>(R.U32(), 0, 4320));
				if (!R.Ok) break;
				LOG_INFO("session %u: stream sender %d fps %d kbps, up to %d lines, encoder '%s'", Params.SessionId, Params.Fps, Params.BitrateKbps,
					Params.MaxHeight, Params.Prefer.c_str());
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Params.SessionId);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Params.SessionId] = std::make_unique<FStreamSenderSession>(Params, Events, SendStreamPacket);
				break;
			}
			case EMsg::OpenStreamReceiver:
			{
				const uint32_t Session = R.U32();
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto Existing = Sessions.find(Session);
				if (Existing != Sessions.end())
				{
					Retire(std::move(Existing->second));
					Sessions.erase(Existing);
				}
				Sessions[Session] = std::make_unique<FStreamReceiverSession>(Session, Events);
				break;
			}
			case EMsg::StreamPacket:
			{
				const uint32_t Session = R.U32();
				FStreamPacket Packet;
				Packet.Kind = EStreamKind(R.U8());
				Packet.Flags = R.U8();
				Packet.PtsMs = R.U32();
				Packet.Codec = R.U32();
				if (!R.Ok || R.Pos > Payload.size()) break;
				Packet.Data.assign(Payload.begin() + R.Pos, Payload.end());
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It != Sessions.end()) It->second->HandleStreamPacket(Packet);
				break;
			}
			case EMsg::StreamControl:
			{
				const uint32_t Session = R.U32();
				const uint8_t Command = R.U8();
				const uint32_t Value = R.U32();
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It != Sessions.end()) It->second->HandleStreamControl(Command, Value);
				break;
			}
			case EMsg::BrowserInput:
			case EMsg::BrowserCommand:
			{
				const uint32_t Session = R.U32();
				const uint8_t Kind = R.U8();
				if (!R.Ok) break;
				std::lock_guard<std::mutex> Lock(SessionsMutex);
				auto It = Sessions.find(Session);
				if (It == Sessions.end()) break;
				if (EMsg(Type) == EMsg::BrowserInput) It->second->HandleInput(EInput(Kind), R);
				else It->second->HandleCommand(ECommand(Kind), R);
				break;
			}
			case EMsg::Shutdown:
				LOG_INFO("shutdown requested");
				ShutdownAll();
				ExitProcess(0);
			default:
				LOG_WARN("unknown message type %u (%zu bytes)", Type, Payload.size());
				break;
			}
		}
		LOG_INFO("pipe closed; exiting");
		ShutdownAll();
		ExitProcess(0);
	}

	// Runs a browser on Args.SelfTest, streams it through a sender and a receiver with LossPercent of the packets dropped,
	// and reports what arrives.
	// Connects to the VNC server at Args.SelfTest (host:port) for Args.Seconds, reports frames, and sends a click, a key
	// and some text so the server's log shows them.
	int RunVncSelfTest(const FArgs& Args)
	{
		FVncParams Params;
		Params.SessionId = 11;
		const size_t Colon = Args.SelfTest.rfind(':');
		Params.Host = Colon == std::string::npos ? Args.SelfTest : Args.SelfTest.substr(0, Colon);
		Params.Port = Colon == std::string::npos ? 5900 : uint16_t(std::atoi(Args.SelfTest.c_str() + Colon + 1));
		Params.Password = Args.VncPassword;
		Params.bRequireTls = Args.VncRequireTls;
		Params.PinnedFingerprint = Args.VncPin;
		Params.WantWidth = uint32_t(std::max(0, Args.VncWantWidth));
		Params.WantHeight = uint32_t(std::max(0, Args.VncWantHeight));
		std::atomic<int> Frames{0};
		std::atomic<int> Width{0}, Height{0};
		std::mutex LastMutex;
		std::vector<uint8_t> Last;
		const int FrameTap = FrameTaps::Add(11, [&](const uint8_t* Bgra, int W, int H)
		{
			++Frames;
			Width = W;
			Height = H;
			std::lock_guard<std::mutex> Lock(LastMutex);
			Last.assign(Bgra, Bgra + size_t(W) * H * 4);
		});
		FSessionEvents Events;
		Events.OnVideo = [](uint32_t, uint32_t Generation, uint32_t W, uint32_t H, const std::string&) { LOG_INFO("selftest: video %ux%u (generation %u)", W, H, Generation); };
		Events.OnState = [](uint32_t, ESessionState State, double, double, const std::string& Message) { LOG_INFO("selftest: state %d %s", int(State), Message.c_str()); };
		{
			FVncSession Session(Params, Events);
			const double Start = NowSeconds();
			bool bInputSent = false;
			while (NowSeconds() - Start < Args.Seconds)
			{
				Sleep(1000);
				LOG_INFO("selftest: %d frames (%dx%d)", Frames.load(), Width.load(), Height.load());
				if (!bInputSent && Frames > 0)
				{
					bInputSent = true;
					auto Input = [&Session](EInput Kind, const FPayloadWriter& W)
					{
						FPayloadReader R(W.Bytes);
						Session.HandleInput(Kind, R);
					};
					FPayloadWriter Move;
					Move.U32(40); Move.U32(30); Move.U32(0);
					Input(EInput::MouseMove, Move);
					for (uint8_t Up : {0, 1})
					{
						FPayloadWriter Click;
						Click.U32(40); Click.U32(30); Click.U8(0); Click.U8(Up); Click.U8(1); Click.U32(0);
						Input(EInput::MouseButton, Click);
					}
					FPayloadWriter Key;
					Key.U8(uint8_t(TSHostProtocol::EKeyType::RawKeyDown)); Key.U32(0x0D); Key.U32(0);
					const uint16_t NoChar = 0;
					Key.Append(&NoChar, 2); Key.U32(0);
					Input(EInput::Key, Key);
					FPayloadWriter Text;
					Text.Str("hi");
					Input(EInput::Text, Text);
				}
			}
		}
		FrameTaps::Remove(FrameTap);
		if (!Args.Out.empty() && !Last.empty())
		{
			// Raw BGRA of the last frame, for comparing with what the server drew.
			if (FILE* File = _wfopen(Args.Out.c_str(), L"wb"))
			{
				fwrite(Last.data(), 1, Last.size(), File);
				fclose(File);
				LOG_INFO("selftest: last frame (%dx%d BGRA) saved", Width.load(), Height.load());
			}
		}
		LOG_INFO("selftest result: %d frames", Frames.load());
		return Frames > 0 ? 0 : 1;
	}

	// Shares the first window whose title contains Args.SelfTest for Args.Seconds and reports frames and sound.
	int RunWindowSelfTest(const FArgs& Args)
	{
		const std::vector<FWindowInfo> Windows = ListShareableWindows(0);
		const FWindowInfo* Pick = nullptr;
		for (const FWindowInfo& Info : Windows)
		{
			LOG_INFO("selftest: window %llx pid %u %s \"%s\"", (unsigned long long)Info.Window, Info.ProcessId, Info.Exe.c_str(), Info.Title.c_str());
			if (!Pick && Info.Title.find(Args.SelfTest) != std::string::npos) Pick = &Info;
		}
		if (!Pick)
		{
			LOG_ERROR("selftest: no window title contains \"%s\"", Args.SelfTest.c_str());
			return 1;
		}
		std::atomic<int> Frames{0};
		std::atomic<int64_t> AudioFrames{0};
		std::atomic<int> Width{0}, Height{0};
		const int FrameTap = FrameTaps::Add(9, [&](const uint8_t*, int W, int H) { ++Frames; Width = W; Height = H; });
		const int AudioTap = AudioTaps::Add(9, [&](const float*, int N) { AudioFrames += N; });
		FSessionEvents Events;
		Events.OnVideo = [](uint32_t, uint32_t Generation, uint32_t W, uint32_t H, const std::string&) { LOG_INFO("selftest: video %ux%u (generation %u)", W, H, Generation); };
		Events.OnState = [](uint32_t, ESessionState State, double, double, const std::string& Message) { LOG_INFO("selftest: state %d %s", int(State), Message.c_str()); };
		FWindowParams Params;
		Params.SessionId = 9;
		Params.Window = Pick->Window;
		{
			FWindowSession Session(Params, Events);
			const double Start = NowSeconds();
			while (NowSeconds() - Start < Args.Seconds)
			{
				Sleep(1000);
				LOG_INFO("selftest: %d frames (%dx%d), %.1f s of sound so far", Frames.load(), Width.load(), Height.load(), AudioFrames.load() / 48000.0);
			}
		}
		FrameTaps::Remove(FrameTap);
		AudioTaps::Remove(AudioTap);
		LOG_INFO("selftest result: %d frames, %.1f s of sound", Frames.load(), AudioFrames.load() / 48000.0);
		return Frames > 0 ? 0 : 1;
	}

	int RunStreamSelfTest(const FArgs& Args)
	{
		std::string Error;
		// The source is a browser page, or with --selftest-stream-file a video file played like a screen does.
		const bool bFromFile = Args.SelfTestLocal;
		if (!bFromFile)
		{
			Browser::AllowFileUrls(true);
			if (!Browser::EnsureInitialized(Args.BrowserDir, Args.BrowserData, LogDirectory(Args.Log), Error))
			{
				LOG_ERROR("selftest: %s", Error.c_str());
				return 1;
			}
		}
		std::atomic<int> VideoFrames{0}, Dropped{0}, Delivered{0};
		std::atomic<FAudioHeader*> Audio{nullptr};
		FSessionEvents ReceiverEvents;
		ReceiverEvents.OnVideo = [](uint32_t, uint32_t Generation, uint32_t Width, uint32_t Height, const std::string&)
		{
			LOG_INFO("selftest: receiver video %ux%u (generation %u)", Width, Height, Generation);
		};
		ReceiverEvents.OnAudio = [&Audio](uint32_t, uint32_t, uint32_t, uint32_t, const std::string& Name)
		{
			HANDLE Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, Widen(Name).c_str());
			Audio = Mapping ? static_cast<FAudioHeader*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
			LOG_INFO("selftest: receiver audio ring mapped=%d", Audio.load() != nullptr);
		};
		ReceiverEvents.OnState = [](uint32_t, ESessionState State, double, double, const std::string& Message)
		{
			LOG_INFO("selftest: receiver state %d %s", int(State), Message.c_str());
		};
		auto Receiver = std::make_unique<FStreamReceiverSession>(3, ReceiverEvents);

		FSessionEvents SenderEvents;
		SenderEvents.OnState = [](uint32_t, ESessionState State, double, double, const std::string& Message)
		{
			LOG_INFO("selftest: sender state %d %s", int(State), Message.c_str());
		};
		uint32_t Seed = 12345;
		bool bWaitKey = false;
		FStreamReceiverSession* RawReceiver = Receiver.get();
		auto Deliver = [&](uint32_t, const FStreamPacket& Packet)
		{
			Seed = Seed * 1664525u + 1013904223u;
			if ((Seed >> 8) % 10000 < uint32_t(Args.LossPercent * 100.0))
			{
				++Dropped;
				if (Packet.Kind == EStreamKind::Video)
				{
					bWaitKey = true;
					RawReceiver->HandleStreamControl(uint8_t(EStreamControl::Reset), 0);
				}
				return;
			}
			++Delivered;
			RawReceiver->HandleStreamPacket(Packet);
		};
		std::unique_ptr<FSession> BrowserSessionPtr;
		std::atomic<FAudioHeader*> SourceAudio{nullptr};
		if (bFromFile)
		{
			FSessionEvents SourceEvents;
			SourceEvents.OnAudio = [&SourceAudio](uint32_t, uint32_t, uint32_t, uint32_t, const std::string& Name)
			{
				HANDLE Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, Widen(Name).c_str());
				SourceAudio = Mapping ? static_cast<FAudioHeader*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
			};
			FOpenParams Params;
			Params.SessionId = 1;
			Params.Url = Args.SelfTest;
			Params.IsLocalFile = true;
			BrowserSessionPtr = std::make_unique<FMediaSession>(Params, Args.YtDlp, SourceEvents);
		}
		else
		{
			FBrowserParams BrowserParams;
			BrowserParams.SessionId = 1;
			BrowserParams.Url = Args.SelfTest;
			BrowserParams.Width = 1280;
			BrowserParams.Height = 720;
			BrowserSessionPtr = std::make_unique<FBrowserSession>(BrowserParams, FSessionEvents());
		}
		FSenderParams SenderParams;
		SenderParams.SessionId = 2;
		SenderParams.SourceSessionId = 1;
		SenderParams.Fps = Args.Bench.Settings.Fps > 0 ? Args.Bench.Settings.Fps : 30;
		SenderParams.BitrateKbps = Args.Bench.Settings.BitrateKbps;
		SenderParams.Prefer = Args.Bench.Settings.Prefer;
		SenderParams.bAllowSoftware = Args.Bench.Settings.bAllowSoftware;
		SenderParams.MaxHeight = Args.StreamHeight;
		auto Sender = std::make_unique<FStreamSenderSession>(SenderParams, SenderEvents, Deliver);

		// Consume audio like the game's mixer and ask for a keyframe after losses, like the mod.
		const double Start = NowSeconds();
		int64_t Pulled = 0, Consumed = 0;
		int64_t SourcePulled = 0;
		while (NowSeconds() - Start < Args.Seconds)
		{
			if (FAudioHeader* Header = Audio.load())
			{
				const int64_t Due = int64_t((NowSeconds() - Start) * 48000);
				int64_t Read = std::max(InterlockedCompareExchange64(&Header->ReadFrames, 0, 0), InterlockedCompareExchange64(&Header->DiscardUntil, 0, 0));
				const int64_t Write = InterlockedCompareExchange64(&Header->WriteFrames, 0, 0);
				const int64_t Take = std::clamp<int64_t>(Due - Pulled, 0, Write - Read);
				InterlockedExchange64(&Header->ReadFrames, Read + Take);
				Consumed += Take;
				Pulled = Due;
			}
			// The video's own sound is played by the game on the sharer's PC: consume it the same way.
			if (FAudioHeader* Header = SourceAudio.load())
			{
				const int64_t Due = int64_t((NowSeconds() - Start) * 48000);
				int64_t Read = std::max(InterlockedCompareExchange64(&Header->ReadFrames, 0, 0), InterlockedCompareExchange64(&Header->DiscardUntil, 0, 0));
				const int64_t Write = InterlockedCompareExchange64(&Header->WriteFrames, 0, 0);
				const int64_t Take = std::clamp<int64_t>(Due - SourcePulled, 0, Write - Read);
				InterlockedExchange64(&Header->ReadFrames, Read + Take);
				SourcePulled = Due;
			}
			if (bFromFile)
			{
				BrowserSessionPtr->SetTarget(NowSeconds() - Start, false);
			}
			if (bWaitKey)
			{
				bWaitKey = false;
				Sender->HandleStreamControl(uint8_t(EStreamControl::Keyframe), 0);
			}
			Sleep(10);
		}
		LOG_INFO("selftest result: packets delivered %d, dropped %d, audio consumed %.1f s", Delivered.load(), Dropped.load(), Consumed / 48000.0);
		Sender.reset();
		BrowserSessionPtr.reset();
		Receiver.reset();
		if (!bFromFile) Browser::Shutdown();
		return Delivered > 0 ? 0 : 1;
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
		Events.OnSubtitleTracks = [](uint32_t, const std::vector<FSubtitleTrack>& Tracks)
		{
			for (const FSubtitleTrack& Track : Tracks)
			{
				LOG_INFO("selftest: subtitle track %u: %s [%s] kind %d %s", Track.Id, Track.Label.c_str(), Track.Language.c_str(), int(Track.Kind), Track.Format.c_str());
			}
		};
		Events.OnSubtitleCues = [](uint32_t, uint32_t Track, bool Replace, const std::vector<FSubtitleCue>& Cues)
		{
			LOG_INFO("selftest: subtitles track %u: %zu cue(s)%s%s", Track, Cues.size(), Replace ? " (replace)" : "",
				Cues.empty() ? "" : (" first " + std::to_string(Cues.front().StartMs) + " ms: " + Cues.front().Text.substr(0, 80)).c_str());
		};

		std::unique_ptr<FSession> Session;
		FBrowserSession* BrowserSession = nullptr;
		if (Args.SelfTestBrowser)
		{
			std::string Error;
			Browser::AllowFileUrls(true);
			if (!Browser::EnsureInitialized(Args.BrowserDir, Args.BrowserData, LogDirectory(Args.Log), Error))
			{
				LOG_ERROR("selftest: %s", Error.c_str());
				return 1;
			}
			FBrowserParams Params;
			Params.SessionId = 1;
			Params.Url = Args.SelfTest;
			Params.Width = 1280;
			Params.Height = 720;
			Events.OnBrowserState = [](uint32_t, const std::string& Url, const std::string& Title, bool Loading, bool Back, bool Forward, uint8_t Cursor,
										 bool Locked)
			{
				LOG_INFO("selftest: browser state url=%s title=%s loading=%d back=%d forward=%d cursor=%u locked=%d", Url.substr(0, 80).c_str(),
					Title.substr(0, 60).c_str(), Loading, Back, Forward, Cursor, Locked);
			};
			auto NewBrowser = std::make_unique<FBrowserSession>(Params, Events);
			BrowserSession = NewBrowser.get();
			Session = std::move(NewBrowser);
		}
		else
		{
			FOpenParams Params;
			Params.SessionId = 1;
			Params.Url = Args.SelfTest;
			Params.IsLocalFile = Args.SelfTestLocal;
			Params.StartSeconds = Args.Start;
			Params.SubtitleLanguage = Args.SubtitleLanguage;
			Session = std::make_unique<FMediaSession>(Params, Args.YtDlp, Events);
		}

		// Scripted input for the browser selftest page (Tools/browser_test.html), at seconds since the start.
		struct FStep
		{
			double At;
			std::function<void()> Run;
			bool Done = false;
		};
		auto Send = [&](EInput Kind, const FPayloadWriter& W)
		{
			FPayloadReader R(W.Bytes);
			Session->HandleInput(Kind, R);
		};
		auto Command = [&](ECommand Kind, const std::string& Text)
		{
			FPayloadWriter W;
			W.Str(Text);
			FPayloadReader R(W.Bytes);
			Session->HandleCommand(Kind, R);
		};
		auto Click = [&](int X, int Y)
		{
			for (int Up = 0; Up < 2; ++Up)
			{
				FPayloadWriter W;
				W.U32(uint32_t(X));
				W.U32(uint32_t(Y));
				W.U8(0);
				W.U8(uint8_t(Up));
				W.U8(1);
				W.U32(0);
				Send(EInput::MouseButton, W);
			}
		};
		auto Move = [&](int X, int Y)
		{
			FPayloadWriter W;
			W.U32(uint32_t(X));
			W.U32(uint32_t(Y));
			W.U32(0);
			Send(EInput::MouseMove, W);
		};
		auto Key = [&](EKeyType KeyType, int Vk, uint16_t Ch)
		{
			FPayloadWriter W;
			W.U8(uint8_t(KeyType));
			W.U32(uint32_t(Vk));
			W.U32(uint32_t(MapVirtualKeyW(Vk, MAPVK_VK_TO_VSC) << 16));
			W.Append(&Ch, 2);
			W.U32(0);
			Send(EInput::Key, W);
		};
		std::vector<FStep> Steps;
		if (Args.SubtitleTrack > 0)
		{
			Steps.push_back({Args.SelfTestLocal ? 1.0 : 8.0, [&]
			{
				FPayloadWriter W;
				W.U32(uint32_t(Args.SubtitleTrack));
				FPayloadReader R(W.Bytes);
				Session->HandleCommand(ECommand::SelectSubtitles, R);
			}});
		}
		if (BrowserSession)
		{
			Steps.push_back({3.0, [&]
			{
				LOG_INFO("selftest: click canvas, type a, click text box, insert hey");
				Move(600, 300);
				Click(600, 300);
				Key(EKeyType::RawKeyDown, 'A', 'a');
				Key(EKeyType::Char, 'A', 'a');
				Key(EKeyType::KeyUp, 'A', 'a');
				Click(170, 311);
				FPayloadWriter W;
				W.Str("hey");
				Send(EInput::Text, W);
			}});
			Steps.push_back({4.0, [&]
			{
				LOG_INFO("selftest: click Lock pointer");
				Move(100, 230);
				Click(100, 230);
			}});
			Steps.push_back({4.6, [&]
			{
				LOG_INFO("selftest: pointer deltas");
				for (int I = 1; I <= 5; ++I)
				{
					FPayloadWriter W;
					W.U32(uint32_t(10));
					W.U32(uint32_t(-5));
					Send(EInput::PointerDelta, W);
				}
			}});
			Steps.push_back({5.2, [&]
			{
				LOG_INFO("selftest: Escape");
				Key(EKeyType::RawKeyDown, VK_ESCAPE, 0);
				Key(EKeyType::KeyUp, VK_ESCAPE, 0);
			}});
			Steps.push_back({6.0, [&]
			{
				LOG_INFO("selftest: wheel");
				FPayloadWriter W;
				W.U32(600);
				W.U32(300);
				W.U32(0);
				W.U32(uint32_t(-120));
				W.U32(0);
				Send(EInput::Wheel, W);
			}});
			Steps.push_back({7.0, [&]
			{
				LOG_INFO("selftest: navigate to chrome://version (should be blocked), then example.com");
				Command(ECommand::Navigate, "chrome://version");
				Command(ECommand::Navigate, "https://example.com");
			}});
		}

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
			for (FStep& Step : Steps)
			{
				if (!Step.Done && Elapsed >= Step.At)
				{
					Step.Done = true;
					Step.Run();
				}
			}

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
			(unsigned long long)(BrowserSession ? BrowserSession->GetFramesPublished() : static_cast<FMediaSession*>(Session.get())->GetFramesPublished()),
			int(State.LastState));

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
		Browser::Shutdown();
		return FramesSeen > 0 || State.AudioFrames > 0 ? 0 : 1;
	}
}

int wmain(int Argc, wchar_t** Argv)
{
	const int SubprocessExit = Browser::RunSubprocessIfNeeded();
	if (SubprocessExit >= 0)
	{
		return SubprocessExit;
	}

	const FArgs Args = ParseArgs(Argc, Argv);
	LogOpen(Args.Log);
	LOG_INFO("TelescreenHost %s starting (pid %lu, protocol %u)", HOST_VERSION, GetCurrentProcessId(), Version);
	timeBeginPeriod(1);
	av_log_set_callback(FFmpegLog);
	avformat_network_init();
	if (Args.Parent) WatchParent(Args.Parent);
	SetYtDlpJsRuntime(Args.JsRuntime);

	if (Args.bBench) return RunEncodeBench(Args.Bench);
	if (Args.SelfTestStream) return RunStreamSelfTest(Args);
	if (Args.SelfTestWindow) return RunWindowSelfTest(Args);
	if (Args.SelfTestVnc) return RunVncSelfTest(Args);
	if (Args.SelfTestDirect) return FDirectHub::SelfTest(Args.Seconds > 0.0 ? Args.Seconds : 6.0);
	if (!Args.SelfTest.empty()) return RunSelfTest(Args);
	if (Args.Pipe.empty())
	{
		LOG_ERROR("usage: TelescreenHost --pipe <name> [--parent <pid>] [--log <file>] [--ytdlp <path>]  |  --selftest <url> ...");
		return 1;
	}
	return RunHost(Args);
}
