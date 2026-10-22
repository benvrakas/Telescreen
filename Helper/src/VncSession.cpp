#include "VncSession.h"
#include "Taps.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

#include "../third_party/miniz/miniz.h"

extern "C"
{
#include <libswscale/swscale.h>
}

using TSHostProtocol::EInput;
using TSHostProtocol::ESessionState;

namespace
{
	constexpr double MinPublishInterval = 1.0 / 60.0;
	constexpr int AudioRate = 48000;

	// RFB encodings and pseudo-encodings.
	constexpr int32_t EncRaw = 0;
	constexpr int32_t EncCopyRect = 1;
	constexpr int32_t EncHextile = 5;
	constexpr int32_t EncZrle = 16;
	constexpr int32_t EncDesktopSize = -223;
	constexpr int32_t EncLastRect = -224;
	constexpr int32_t EncCursor = -239;
	constexpr int32_t EncQemuAudio = -259;

	uint16_t BE16(const uint8_t* P) { return uint16_t(P[0] << 8 | P[1]); }
	uint32_t BE32(const uint8_t* P) { return uint32_t(P[0]) << 24 | uint32_t(P[1]) << 16 | uint32_t(P[2]) << 8 | P[3]; }
	void PutBE16(std::vector<uint8_t>& Out, uint16_t V) { Out.push_back(uint8_t(V >> 8)); Out.push_back(uint8_t(V)); }
	void PutBE32(std::vector<uint8_t>& Out, uint32_t V) { for (int S = 24; S >= 0; S -= 8) Out.push_back(uint8_t(V >> S)); }

	// VNC Authentication: the challenge DES-encrypted (ECB) with the password, whose bytes are bit-reversed.
	bool DesEncryptChallenge(const std::string& Password, uint8_t Challenge[16])
	{
		uint8_t Key[8] = {};
		for (size_t I = 0; I < 8 && I < Password.size(); ++I)
		{
			uint8_t B = uint8_t(Password[I]), R = 0;
			for (int Bit = 0; Bit < 8; ++Bit) R |= ((B >> Bit) & 1) << (7 - Bit);
			Key[I] = R;
		}
		BCRYPT_ALG_HANDLE Alg = nullptr;
		BCRYPT_KEY_HANDLE KeyHandle = nullptr;
		ULONG Done = 0;
		bool bOk = BCryptOpenAlgorithmProvider(&Alg, BCRYPT_DES_ALGORITHM, nullptr, 0) == 0 &&
			BCryptSetProperty(Alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
			BCryptGenerateSymmetricKey(Alg, &KeyHandle, nullptr, 0, Key, sizeof(Key), 0) == 0 &&
			BCryptEncrypt(KeyHandle, Challenge, 16, nullptr, nullptr, 0, Challenge, 16, &Done, 0) == 0 && Done == 16;
		if (KeyHandle) BCryptDestroyKey(KeyHandle);
		if (Alg) BCryptCloseAlgorithmProvider(Alg, 0);
		return bOk;
	}

	// X keysym for a Windows virtual key that has no character (0 if it has one or isn't mapped).
	uint32_t KeysymForVirtualKey(int Vk)
	{
		switch (Vk)
		{
		case 0x08: return 0xff08;
		case 0x09: return 0xff09;
		case 0x0D: return 0xff0d;
		case 0x13: return 0xff13;
		case 0x14: return 0xffe5;
		case 0x1B: return 0xff1b;
		case 0x21: return 0xff55;
		case 0x22: return 0xff56;
		case 0x23: return 0xff57;
		case 0x24: return 0xff50;
		case 0x25: return 0xff51;
		case 0x26: return 0xff52;
		case 0x27: return 0xff53;
		case 0x28: return 0xff54;
		case 0x2C: return 0xff61;
		case 0x2D: return 0xff63;
		case 0x2E: return 0xffff;
		case 0x5B: return 0xffeb;
		case 0x5C: return 0xffec;
		case 0x5D: return 0xff67;
		case 0x90: return 0xff7f;
		case 0x91: return 0xff14;
		case 0x10: case 0xA0: return 0xffe1;
		case 0xA1: return 0xffe2;
		case 0x11: case 0xA2: return 0xffe3;
		case 0xA3: return 0xffe4;
		case 0x12: case 0xA4: return 0xffe9;
		case 0xA5: return 0xffea;
		default:
			if (Vk >= 0x70 && Vk <= 0x7B) return 0xffbe + uint32_t(Vk - 0x70);
			return 0;
		}
	}

	uint32_t KeysymForChar(uint32_t Char)
	{
		return Char >= 0x20 && Char <= 0xFF ? Char : (0x01000000u | Char);
	}

	// TELESCREEN_VNC_TRACE=1 logs every message and rectangle (for diagnosing servers that disconnect).
	bool Trace()
	{
		static const bool bTrace = []
		{
			char Value[8] = {};
			return GetEnvironmentVariableA("TELESCREEN_VNC_TRACE", Value, sizeof(Value)) > 0 && Value[0] == '1';
		}();
		return bTrace;
	}

	bool InitWinsock()
	{
		static const bool bOk = []
		{
			WSADATA Data;
			return WSAStartup(MAKEWORD(2, 2), &Data) == 0;
		}();
		return bOk;
	}
}

FVncSession::FVncSession(const FVncParams& InParams, FSessionEvents InEvents)
	: Params(InParams), Events(std::move(InEvents))
{
	FrameTaps::SetRefresh(Params.SessionId, [this]
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		if (!View.empty())
		{
			if (VideoWriter) VideoWriter->Publish(View.data(), NowSeconds());
			FrameTaps::Publish(Params.SessionId, View.data(), ViewWidth, ViewHeight);
		}
	});
	LOG_INFO("session %u: VNC %s:%u (%s password, sound %d)", Params.SessionId, Params.Host.c_str(), Params.Port, Params.Password.empty() ? "no" : "with",
		int(Params.bAudio));
	Worker = std::thread([this] { Run(); });
}

FVncSession::~FVncSession()
{
	FrameTaps::ClearRefresh(Params.SessionId);
	Stopping = true;
	if (Socket != ~uintptr_t(0))
	{
		shutdown(SOCKET(Socket), SD_BOTH);
	}
	if (Worker.joinable()) Worker.join();
	if (Socket != ~uintptr_t(0))
	{
		closesocket(SOCKET(Socket));
	}
	if (Zrle)
	{
		mz_inflateEnd(static_cast<mz_stream*>(Zrle));
		delete static_cast<mz_stream*>(Zrle);
	}
	sws_freeContext(Scaler);
	LOG_INFO("session %u: VNC closed", Params.SessionId);
}

void FVncSession::SetState(ESessionState State, const std::string& Message)
{
	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		if (State == LastState && Message == LastMessage) return;
		LastState = State;
		LastMessage = Message;
	}
	if (Events.OnState) Events.OnState(Params.SessionId, State, 0.0, 0.0, Message);
}

bool FVncSession::ReadExact(void* Data, size_t Size)
{
	uint8_t* Out = static_cast<uint8_t*>(Data);
	while (Size > 0 && !Stopping)
	{
		const int Got = recv(SOCKET(Socket), reinterpret_cast<char*>(Out), int(std::min<size_t>(Size, 1 << 20)), 0);
		if (Got <= 0) return false;
		Out += Got;
		Size -= size_t(Got);
	}
	return Size == 0;
}

bool FVncSession::Skip(size_t Size)
{
	uint8_t Buffer[4096];
	while (Size > 0)
	{
		const size_t Chunk = std::min(Size, sizeof(Buffer));
		if (!ReadExact(Buffer, Chunk)) return false;
		Size -= Chunk;
	}
	return true;
}

bool FVncSession::Send(const void* Data, size_t Size)
{
	if (Trace()) LOG_INFO("session %u: trace: client message %u (%zu bytes)", Params.SessionId, Size ? static_cast<const uint8_t*>(Data)[0] : 0, Size);
	std::lock_guard<std::mutex> Lock(SendMutex);
	const char* In = static_cast<const char*>(Data);
	while (Size > 0)
	{
		const int Sent = send(SOCKET(Socket), In, int(Size), 0);
		if (Sent <= 0) return false;
		In += Sent;
		Size -= size_t(Sent);
	}
	return true;
}

bool FVncSession::Connect(std::string& Error)
{
	if (!InitWinsock())
	{
		Error = "networking is unavailable";
		return false;
	}
	addrinfo Hints{};
	Hints.ai_family = AF_UNSPEC;
	Hints.ai_socktype = SOCK_STREAM;
	Hints.ai_protocol = IPPROTO_TCP;
	addrinfo* Results = nullptr;
	const std::string Port = std::to_string(Params.Port);
	if (getaddrinfo(Params.Host.c_str(), Port.c_str(), &Hints, &Results) != 0 || !Results)
	{
		Error = "couldn't find " + Params.Host;
		return false;
	}
	int LastError = 0;
	for (addrinfo* It = Results; It && !Stopping; It = It->ai_next)
	{
		SOCKET S = socket(It->ai_family, It->ai_socktype, It->ai_protocol);
		if (S == INVALID_SOCKET) continue;
		if (connect(S, It->ai_addr, int(It->ai_addrlen)) == 0)
		{
			const BOOL NoDelay = TRUE;
			setsockopt(S, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));
			Socket = uintptr_t(S);
			break;
		}
		LastError = WSAGetLastError();
		closesocket(S);
	}
	freeaddrinfo(Results);
	if (Socket == ~uintptr_t(0))
	{
		Error = "couldn't connect to " + Params.Host + ":" + Port + " (error " + std::to_string(LastError) + ")";
		return false;
	}
	return true;
}

bool FVncSession::Handshake(std::string& Error)
{
	char Version[13] = {};
	if (!ReadExact(Version, 12) || std::strncmp(Version, "RFB ", 4) != 0)
	{
		Error = "that isn't a VNC server";
		return false;
	}
	const int Major = std::atoi(Version + 4), Minor = std::atoi(Version + 8);
	const int Use = Major > 3 || Minor >= 8 ? 8 : Minor >= 7 ? 7 : 3;
	const std::string Reply = "RFB 003.00" + std::to_string(Use) + "\n";
	if (!Send(Reply.data(), Reply.size())) return false;
	LOG_INFO("session %u: server %.11s, using 3.%d", Params.SessionId, Version, Use);

	auto ReadReason = [this, &Error](const char* Prefix)
	{
		uint8_t Len[4];
		std::string Reason;
		if (ReadExact(Len, 4))
		{
			Reason.resize(std::min<uint32_t>(BE32(Len), 1000));
			ReadExact(Reason.data(), Reason.size());
		}
		Error = std::string(Prefix) + (Reason.empty() ? "" : ": " + Reason);
	};

	uint8_t Security = 0;
	if (Use == 3)
	{
		uint8_t Type[4];
		if (!ReadExact(Type, 4)) return false;
		Security = uint8_t(BE32(Type));
		if (Security == 0)
		{
			ReadReason("the server refused the connection");
			return false;
		}
	}
	else
	{
		uint8_t Count = 0;
		if (!ReadExact(&Count, 1)) return false;
		if (Count == 0)
		{
			ReadReason("the server refused the connection");
			return false;
		}
		std::vector<uint8_t> Types(Count);
		if (!ReadExact(Types.data(), Count)) return false;
		const bool bHasNone = std::find(Types.begin(), Types.end(), 1) != Types.end();
		const bool bHasVnc = std::find(Types.begin(), Types.end(), 2) != Types.end();
		Security = !Params.Password.empty() && bHasVnc ? 2 : bHasNone ? 1 : bHasVnc ? 2 : 0;
		if (Security == 0)
		{
			std::string Offered;
			for (uint8_t T : Types) Offered += (Offered.empty() ? "" : ", ") + std::to_string(T);
			Error = "the server needs a login Telescreen doesn't support yet (security types " + Offered + "; supported: none, VNC password)";
			return false;
		}
		if (!Send(&Security, 1)) return false;
	}
	if (Security == 2)
	{
		uint8_t Challenge[16];
		if (!ReadExact(Challenge, 16)) return false;
		if (!DesEncryptChallenge(Params.Password, Challenge))
		{
			Error = "couldn't encrypt the password";
			return false;
		}
		if (!Send(Challenge, 16)) return false;
	}
	if (Security == 2 || Use == 8)
	{
		uint8_t Result[4];
		if (!ReadExact(Result, 4)) return false;
		if (BE32(Result) != 0)
		{
			if (Use == 8)
			{
				ReadReason(Security == 2 ? "the server refused the password" : "the server refused the connection");
			}
			else
			{
				Error = Security == 2 ? "the server refused the password" : "the server refused the connection";
			}
			return false;
		}
	}
	LOG_INFO("session %u: security %s accepted", Params.SessionId, Security == 2 ? "VNC password" : "none");

	const uint8_t Shared = 1;
	if (!Send(&Shared, 1)) return false;
	uint8_t Init[24];
	if (!ReadExact(Init, 24)) return false;
	std::string Name(std::min<uint32_t>(BE32(Init + 20), 4096), '\0');
	if (!ReadExact(Name.data(), Name.size())) return false;
	Resize(BE16(Init), BE16(Init + 2));
	LOG_INFO("session %u: desktop \"%s\" %dx%d", Params.SessionId, Name.c_str(), FbWidth, FbHeight);

	// SetPixelFormat: 32 bpp, depth 24, little endian, true colour, 8 bits per channel at shifts 16/8/0 (BGRX in memory).
	std::vector<uint8_t> Message = {0, 0, 0, 0, 32, 24, 0, 1};
	PutBE16(Message, 255);
	PutBE16(Message, 255);
	PutBE16(Message, 255);
	Message.insert(Message.end(), {16, 8, 0, 0, 0, 0});
	// SetEncodings, preferred first.
	std::vector<int32_t> Encodings = {EncZrle, EncHextile, EncCopyRect, EncRaw, EncDesktopSize, EncLastRect, EncCursor};
	if (Params.bAudio) Encodings.push_back(EncQemuAudio);
	Message.insert(Message.end(), {2, 0});
	PutBE16(Message, uint16_t(Encodings.size()));
	for (int32_t E : Encodings) PutBE32(Message, uint32_t(E));
	if (!Send(Message.data(), Message.size())) return false;
	RequestUpdate(false);
	return true;
}

void FVncSession::Resize(int W, int H)
{
	std::lock_guard<std::mutex> Lock(FrameMutex);
	FbWidth = std::clamp(W, 1, 8192);
	FbHeight = std::clamp(H, 1, 8192);
	Framebuffer.assign(size_t(FbWidth) * FbHeight, 0);
	bDirty = true;
}

void FVncSession::RequestUpdate(bool bIncremental)
{
	std::vector<uint8_t> Message = {3, uint8_t(bIncremental ? 1 : 0)};
	PutBE16(Message, 0);
	PutBE16(Message, 0);
	PutBE16(Message, uint16_t(FbWidth));
	PutBE16(Message, uint16_t(FbHeight));
	Send(Message.data(), Message.size());
}

void FVncSession::Run()
{
	SetState(ESessionState::Resolving, "Connecting to " + Params.Host + ":" + std::to_string(Params.Port) + "...");
	std::string Error;
	if (!Connect(Error) || !Handshake(Error))
	{
		if (!Stopping)
		{
			if (Error.empty()) Error = "the connection closed during login";
			LOG_WARN("session %u: VNC %s", Params.SessionId, Error.c_str());
			SetState(ESessionState::Error, "VNC: " + Error);
		}
		return;
	}
	while (!Stopping)
	{
		fd_set Read;
		FD_ZERO(&Read);
		FD_SET(SOCKET(Socket), &Read);
		timeval Wait{0, 20000};
		const int Ready = select(0, &Read, nullptr, nullptr, &Wait);
		if (Ready < 0) break;
		if (Ready == 0)
		{
			Publish();
			continue;
		}
		uint8_t Type = 0;
		if (!ReadExact(&Type, 1)) break;
		if (Trace()) LOG_INFO("session %u: trace: server message %u", Params.SessionId, Type);
		bool bOk = true;
		switch (Type)
		{
		case 0:
			bOk = HandleFramebufferUpdate();
			if (bOk) RequestUpdate(true);
			Publish();
			break;
		case 1:
		{
			uint8_t Head[5];
			bOk = ReadExact(Head, 5) && Skip(size_t(BE16(Head + 3)) * 6);
			break;
		}
		case 2:
			break;
		case 3:
		{
			uint8_t Head[7];
			bOk = ReadExact(Head, 7);
			// Extended clipboard sends a negative length; its payload is the absolute value.
			const int32_t Len = int32_t(BE32(Head + 3));
			bOk = bOk && Skip(size_t(Len < 0 ? -int64_t(Len) : Len));
			break;
		}
		case 255:
			bOk = HandleQemuMessage();
			break;
		default:
			LOG_WARN("session %u: unknown VNC message %u; disconnecting", Params.SessionId, Type);
			bOk = false;
			break;
		}
		if (!bOk) break;
	}
	if (!Stopping)
	{
		LOG_INFO("session %u: VNC ended: %s", Params.SessionId, DecodeError.empty() ? "the server closed the connection" : DecodeError.c_str());
		SetState(ESessionState::Error, DecodeError.empty() ? "The VNC server closed the connection." : DecodeError);
	}
}

bool FVncSession::HandleFramebufferUpdate()
{
	uint8_t Head[3];
	if (!ReadExact(Head, 3)) return false;
	const int Count = BE16(Head + 1);
	for (int I = 0; I < Count; ++I)
	{
		uint8_t Rect[12];
		if (!ReadExact(Rect, 12)) return false;
		const int X = BE16(Rect), Y = BE16(Rect + 2), W = BE16(Rect + 4), H = BE16(Rect + 6);
		const int32_t Encoding = int32_t(BE32(Rect + 8));
		if (Trace()) LOG_INFO("session %u: trace: rect %d,%d %dx%d encoding %d", Params.SessionId, X, Y, W, H, Encoding);
		bool bOk = true;
		switch (Encoding)
		{
		case EncRaw: bOk = DecodeRaw(X, Y, W, H); break;
		case EncCopyRect: bOk = DecodeCopyRect(X, Y, W, H); break;
		case EncHextile: bOk = DecodeHextile(X, Y, W, H); break;
		case EncZrle: bOk = DecodeZrle(X, Y, W, H); break;
		case EncDesktopSize:
			LOG_INFO("session %u: desktop resized to %dx%d", Params.SessionId, W, H);
			Resize(W, H);
			break;
		case EncCursor: bOk = DecodeCursor(X, Y, W, H); break;
		case EncLastRect: return true;
		case EncQemuAudio:
		{
			// The server takes QEMU audio: ask for 48 kHz stereo 16-bit and start it.
			std::vector<uint8_t> Message = {255, 1};
			PutBE16(Message, 2);
			Message.push_back(3);
			Message.push_back(2);
			PutBE32(Message, AudioRate);
			Message.insert(Message.end(), {255, 1});
			PutBE16(Message, 0);
			Send(Message.data(), Message.size());
			bAudioOn = true;
			LOG_INFO("session %u: QEMU audio on", Params.SessionId);
			break;
		}
		default:
			LOG_WARN("session %u: unexpected VNC encoding %d; disconnecting", Params.SessionId, Encoding);
			return false;
		}
		if (!bOk)
		{
			LOG_WARN("session %u: couldn't read a %dx%d rectangle (encoding %d)", Params.SessionId, W, H, Encoding);
			DecodeError = "Telescreen couldn't read the VNC server's picture (encoding " + std::to_string(Encoding) + ").";
			return false;
		}
	}
	return true;
}

void FVncSession::Fill(int X, int Y, int W, int H, uint32_t Pixel)
{
	for (int Row = std::max(Y, 0); Row < std::min(Y + H, FbHeight); ++Row)
	{
		uint32_t* Line = Framebuffer.data() + size_t(Row) * FbWidth;
		std::fill(Line + std::max(X, 0), Line + std::min(X + W, FbWidth), Pixel);
	}
}

bool FVncSession::DecodeRaw(int X, int Y, int W, int H)
{
	std::vector<uint32_t> Pixels(size_t(W) * H);
	if (!ReadExact(Pixels.data(), Pixels.size() * 4)) return false;
	std::lock_guard<std::mutex> Lock(FrameMutex);
	for (int Row = 0; Row < H; ++Row)
	{
		if (Y + Row < 0 || Y + Row >= FbHeight) continue;
		const int Copy = std::min(W, FbWidth - X);
		if (Copy > 0) memcpy(&Framebuffer[size_t(Y + Row) * FbWidth + X], &Pixels[size_t(Row) * W], size_t(Copy) * 4);
	}
	bDirty = true;
	return true;
}

bool FVncSession::DecodeCopyRect(int X, int Y, int W, int H)
{
	uint8_t Src[4];
	if (!ReadExact(Src, 4)) return false;
	const int SrcX = BE16(Src), SrcY = BE16(Src + 2);
	std::lock_guard<std::mutex> Lock(FrameMutex);
	if (X + W > FbWidth || Y + H > FbHeight || SrcX + W > FbWidth || SrcY + H > FbHeight) return true;
	std::vector<uint32_t> Copy(size_t(W) * H);
	for (int Row = 0; Row < H; ++Row) memcpy(&Copy[size_t(Row) * W], &Framebuffer[size_t(SrcY + Row) * FbWidth + SrcX], size_t(W) * 4);
	for (int Row = 0; Row < H; ++Row) memcpy(&Framebuffer[size_t(Y + Row) * FbWidth + X], &Copy[size_t(Row) * W], size_t(W) * 4);
	bDirty = true;
	return true;
}

bool FVncSession::DecodeHextile(int X, int Y, int W, int H)
{
	uint32_t Background = 0, Foreground = 0;
	for (int TileY = Y; TileY < Y + H; TileY += 16)
	{
		for (int TileX = X; TileX < X + W; TileX += 16)
		{
			const int TW = std::min(16, X + W - TileX), TH = std::min(16, Y + H - TileY);
			uint8_t Sub = 0;
			if (!ReadExact(&Sub, 1)) return false;
			if (Sub & 1)
			{
				if (!DecodeRaw(TileX, TileY, TW, TH)) return false;
				continue;
			}
			if ((Sub & 2) && !ReadExact(&Background, 4)) return false;
			if ((Sub & 4) && !ReadExact(&Foreground, 4)) return false;
			std::lock_guard<std::mutex> Lock(FrameMutex);
			Fill(TileX, TileY, TW, TH, Background);
			if (Sub & 8)
			{
				uint8_t Count = 0;
				if (!ReadExact(&Count, 1)) return false;
				for (int I = 0; I < Count; ++I)
				{
					uint32_t Colour = Foreground;
					if ((Sub & 16) && !ReadExact(&Colour, 4)) return false;
					uint8_t XY[2];
					if (!ReadExact(XY, 2)) return false;
					Fill(TileX + (XY[0] >> 4), TileY + (XY[0] & 15), (XY[1] >> 4) + 1, (XY[1] & 15) + 1, Colour);
				}
			}
			bDirty = true;
		}
	}
	return true;
}

bool FVncSession::DecodeZrle(int X, int Y, int W, int H)
{
	uint8_t Len[4];
	if (!ReadExact(Len, 4)) return false;
	ZrleIn.resize(BE32(Len));
	if (!ReadExact(ZrleIn.data(), ZrleIn.size())) return false;
	if (!Zrle)
	{
		Zrle = new mz_stream{};
		if (mz_inflateInit(static_cast<mz_stream*>(Zrle)) != MZ_OK) return false;
	}
	mz_stream* Z = static_cast<mz_stream*>(Zrle);
	Z->next_in = ZrleIn.data();
	Z->avail_in = unsigned(ZrleIn.size());
	ZrleOut.clear();
	uint8_t Chunk[65536];
	// Inflate until the input is used up and no output is pending: a rectangle can expand past one chunk after the last
	// input byte is read (a full chunk means the inflater may hold more).
	for (;;)
	{
		Z->next_out = Chunk;
		Z->avail_out = sizeof(Chunk);
		const int Result = mz_inflate(Z, MZ_SYNC_FLUSH);
		if (Result != MZ_OK && Result != MZ_BUF_ERROR && Result != MZ_STREAM_END)
		{
			LOG_WARN("session %u: ZRLE inflate failed (%d)", Params.SessionId, Result);
			return false;
		}
		const size_t Produced = sizeof(Chunk) - Z->avail_out;
		ZrleOut.insert(ZrleOut.end(), Chunk, Chunk + Produced);
		if ((Z->avail_in == 0 && Z->avail_out != 0) || (Produced == 0 && Result == MZ_BUF_ERROR)) break;
	}
	size_t Pos = 0;
	auto Need = [&](size_t N) { return Pos + N <= ZrleOut.size(); };
	// CPIXEL: the three low bytes of a 32-bit BGRX pixel.
	auto Cpixel = [&]() -> uint32_t
	{
		const uint32_t P = uint32_t(ZrleOut[Pos]) | uint32_t(ZrleOut[Pos + 1]) << 8 | uint32_t(ZrleOut[Pos + 2]) << 16;
		Pos += 3;
		return P;
	};
	std::lock_guard<std::mutex> Lock(FrameMutex);
	for (int TileY = Y; TileY < Y + H; TileY += 64)
	{
		for (int TileX = X; TileX < X + W; TileX += 64)
		{
			const int TW = std::min(64, X + W - TileX), TH = std::min(64, Y + H - TileY);
			if (!Need(1))
			{
				LOG_WARN("session %u: ZRLE data for %dx%d ended early (%zu bytes)", Params.SessionId, W, H, ZrleOut.size());
				return false;
			}
			const uint8_t Sub = ZrleOut[Pos++];
			auto Put = [&](int I, uint32_t P)
			{
				const int PX = TileX + I % TW, PY = TileY + I / TW;
				if (PX < FbWidth && PY < FbHeight) Framebuffer[size_t(PY) * FbWidth + PX] = P;
			};
			if (Sub == 0)
			{
				if (!Need(size_t(TW) * TH * 3)) return false;
				for (int I = 0; I < TW * TH; ++I) Put(I, Cpixel());
			}
			else if (Sub == 1)
			{
				if (!Need(3)) return false;
				Fill(TileX, TileY, TW, TH, Cpixel());
			}
			else if (Sub >= 2 && Sub <= 16)
			{
				if (!Need(size_t(Sub) * 3)) return false;
				uint32_t Palette[16];
				for (int I = 0; I < Sub; ++I) Palette[I] = Cpixel();
				const int Bits = Sub == 2 ? 1 : Sub <= 4 ? 2 : 4;
				const int RowBytes = (TW * Bits + 7) / 8;
				if (!Need(size_t(RowBytes) * TH)) return false;
				for (int Row = 0; Row < TH; ++Row)
				{
					const uint8_t* Line = &ZrleOut[Pos + size_t(Row) * RowBytes];
					for (int Col = 0; Col < TW; ++Col)
					{
						const int Bit = Col * Bits;
						const int Index = (Line[Bit / 8] >> (8 - Bits - Bit % 8)) & ((1 << Bits) - 1);
						Put(Row * TW + Col, Palette[std::min(Index, Sub - 1)]);
					}
				}
				Pos += size_t(RowBytes) * TH;
			}
			else if (Sub == 128 || Sub >= 130)
			{
				uint32_t Palette[128];
				const int PaletteSize = Sub == 128 ? 0 : Sub - 128;
				if (!Need(size_t(PaletteSize) * 3)) return false;
				for (int I = 0; I < PaletteSize; ++I) Palette[I] = Cpixel();
				int I = 0;
				while (I < TW * TH)
				{
					uint32_t P = 0;
					int Run = 1;
					if (Sub == 128)
					{
						if (!Need(3)) return false;
						P = Cpixel();
						Run = -1;
					}
					else
					{
						if (!Need(1)) return false;
						const uint8_t Index = ZrleOut[Pos++];
						P = Palette[std::min<int>(Index & 127, PaletteSize - 1)];
						if (Index & 128) Run = -1;
					}
					if (Run < 0)
					{
						Run = 1;
						uint8_t B = 255;
						while (B == 255)
						{
							if (!Need(1)) return false;
							B = ZrleOut[Pos++];
							Run += B;
						}
					}
					for (int K = 0; K < Run && I < TW * TH; ++K) Put(I++, P);
				}
			}
			else
			{
				LOG_WARN("session %u: bad ZRLE subencoding %u", Params.SessionId, Sub);
				return false;
			}
		}
	}
	bDirty = true;
	return true;
}

bool FVncSession::DecodeCursor(int HotX, int HotY, int W, int H)
{
	std::vector<uint32_t> Pixels(size_t(W) * H);
	std::vector<uint8_t> Mask(size_t((W + 7) / 8) * H);
	if (!ReadExact(Pixels.data(), Pixels.size() * 4) || !ReadExact(Mask.data(), Mask.size())) return false;
	std::lock_guard<std::mutex> Lock(FrameMutex);
	CursorPixels = std::move(Pixels);
	CursorMask = std::move(Mask);
	CursorWidth = W;
	CursorHeight = H;
	CursorHotX = HotX;
	CursorHotY = HotY;
	bDirty = true;
	return true;
}

bool FVncSession::HandleQemuMessage()
{
	uint8_t Head[3];
	if (!ReadExact(Head, 3)) return false;
	if (Head[0] != 1)
	{
		LOG_WARN("session %u: unknown QEMU server message %u; disconnecting", Params.SessionId, Head[0]);
		return false;
	}
	const uint16_t Op = BE16(Head + 1);
	if (Trace()) LOG_INFO("session %u: trace: QEMU message %u op %u", Params.SessionId, Head[0], Op);
	if (Op != 2) return true;
	uint8_t Len[4];
	if (!ReadExact(Len, 4)) return false;
	const uint32_t Bytes = BE32(Len);
	std::vector<int16_t> Samples(Bytes / 2);
	if (!ReadExact(Samples.data(), Samples.size() * 2) || !Skip(Bytes % 2)) return false;
	if (!AudioWriter)
	{
		AudioWriter = std::make_unique<FAudioWriter>();
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-audio";
		if (!AudioWriter->Create(Name, AudioRate, 2, AudioRate))
		{
			AudioWriter.reset();
			return true;
		}
		if (Events.OnAudio) Events.OnAudio(Params.SessionId, AudioRate, 2, AudioRate, Name);
	}
	AudioFloats.resize(Samples.size());
	for (size_t I = 0; I < Samples.size(); ++I) AudioFloats[I] = Samples[I] / 32768.f;
	const int Frames = int(AudioFloats.size() / 2);
	// Keep the latency low: drop what the game hasn't played if it falls behind.
	if (AudioWriter->QueuedFrames() > AudioRate / 4) AudioWriter->DiscardQueued();
	AudioWriter->Write(AudioFloats.data(), uint32_t(Frames));
	AudioTaps::Publish(Params.SessionId, AudioFloats.data(), Frames);
	return true;
}

void FVncSession::Publish()
{
	std::lock_guard<std::mutex> Lock(FrameMutex);
	const double Now = NowSeconds();
	if (!bDirty || Now - LastPublish < MinPublishInterval || Framebuffer.empty()) return;
	LastPublish = Now;
	bDirty = false;
	PublishLocked();
}

void FVncSession::PublishLocked()
{
	// The framebuffer with the server's pointer drawn where the controller's mouse is.
	std::vector<uint32_t> Frame = Framebuffer;
	if (!CursorPixels.empty())
	{
		const int MaskRow = (CursorWidth + 7) / 8;
		for (int Row = 0; Row < CursorHeight; ++Row)
		{
			const int FY = PointerY - CursorHotY + Row;
			if (FY < 0 || FY >= FbHeight) continue;
			for (int Col = 0; Col < CursorWidth; ++Col)
			{
				const int FX = PointerX - CursorHotX + Col;
				if (FX < 0 || FX >= FbWidth) continue;
				if (CursorMask[size_t(Row) * MaskRow + Col / 8] & (0x80 >> (Col % 8)))
				{
					Frame[size_t(FY) * FbWidth + FX] = CursorPixels[size_t(Row) * CursorWidth + Col];
				}
			}
		}
	}
	for (uint32_t& P : Frame) P |= 0xFF000000u;
	const double Scale = std::min({1.0, double(Params.MaxWidth) / FbWidth, double(Params.MaxHeight) / FbHeight});
	const int OutWidth = std::max(2, int(FbWidth * Scale) & ~1);
	const int OutHeight = std::max(2, int(FbHeight * Scale) & ~1);
	View.resize(size_t(OutWidth) * OutHeight * 4);
	Scaler = sws_getCachedContext(Scaler, FbWidth, FbHeight, AV_PIX_FMT_BGRA, OutWidth, OutHeight, AV_PIX_FMT_BGRA, SWS_AREA, nullptr, nullptr, nullptr);
	if (!Scaler) return;
	const uint8_t* Src[1] = {reinterpret_cast<const uint8_t*>(Frame.data())};
	const int SrcStride[1] = {FbWidth * 4};
	uint8_t* Dst[1] = {View.data()};
	const int DstStride[1] = {OutWidth * 4};
	sws_scale(Scaler, Src, SrcStride, 0, FbHeight, Dst, DstStride);
	ViewWidth = OutWidth;
	ViewHeight = OutHeight;
	if (!VideoWriter || VideoWriter->GetWidth() != uint32_t(ViewWidth) || VideoWriter->GetHeight() != uint32_t(ViewHeight))
	{
		auto Writer = std::make_unique<FVideoWriter>();
		const uint32_t Generation = ++VideoGeneration;
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-video" +
			std::to_string(Generation);
		if (!Writer->Create(Name, ViewWidth, ViewHeight)) return;
		VideoWriter = std::move(Writer);
		LOG_INFO("session %u: VNC video %dx%d (desktop %dx%d, generation %u)", Params.SessionId, ViewWidth, ViewHeight, FbWidth, FbHeight, Generation);
		if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, ViewWidth, ViewHeight, Name);
		SetState(ESessionState::Playing, std::string());
	}
	VideoWriter->Publish(View.data(), NowSeconds());
	FrameTaps::Publish(Params.SessionId, View.data(), ViewWidth, ViewHeight);
}

void FVncSession::SendPointer()
{
	std::vector<uint8_t> Message = {5, Buttons.load()};
	PutBE16(Message, uint16_t(std::clamp(PointerX.load(), 0, std::max(FbWidth - 1, 0))));
	PutBE16(Message, uint16_t(std::clamp(PointerY.load(), 0, std::max(FbHeight - 1, 0))));
	Send(Message.data(), Message.size());
	if (!CursorPixels.empty())
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		bDirty = true;
	}
}

void FVncSession::SendKey(uint32_t Keysym, bool bDown)
{
	std::vector<uint8_t> Message = {4, uint8_t(bDown ? 1 : 0), 0, 0};
	PutBE32(Message, Keysym);
	Send(Message.data(), Message.size());
}

void FVncSession::HandleInput(EInput Kind, FPayloadReader& R)
{
	if (Socket == ~uintptr_t(0) || FbWidth <= 0 || ViewWidth <= 0) return;
	// Picture pixels (the scaled view) to desktop pixels.
	auto ToDesktop = [this](int X, int Y)
	{
		PointerX = int(int64_t(X) * FbWidth / std::max(ViewWidth, 1));
		PointerY = int(int64_t(Y) * FbHeight / std::max(ViewHeight, 1));
	};
	switch (Kind)
	{
	case EInput::MouseMove:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		R.U32();
		if (!R.Ok) break;
		ToDesktop(X, Y);
		SendPointer();
		break;
	}
	case EInput::MouseButton:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		const uint8_t Button = R.U8();
		const bool bUp = R.U8() != 0;
		R.U8();
		R.U32();
		if (!R.Ok) break;
		ToDesktop(X, Y);
		const uint8_t Bit = Button == 1 ? 2 : Button == 2 ? 4 : 1;
		Buttons = bUp ? uint8_t(Buttons & ~Bit) : uint8_t(Buttons | Bit);
		SendPointer();
		break;
	}
	case EInput::Wheel:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		const int DeltaX = int32_t(R.U32()), DeltaY = int32_t(R.U32());
		R.U32();
		if (!R.Ok) break;
		ToDesktop(X, Y);
		// Wheel steps are clicks of buttons 4/5 (vertical) and 6/7 (horizontal).
		auto Click = [this](uint8_t Bit, int Steps)
		{
			for (int I = 0; I < std::clamp(Steps, 1, 10); ++I)
			{
				Buttons = uint8_t(Buttons | Bit);
				SendPointer();
				Buttons = uint8_t(Buttons & ~Bit);
				SendPointer();
			}
		};
		if (DeltaY != 0) Click(DeltaY > 0 ? 8 : 16, std::abs(DeltaY) / 120);
		if (DeltaX != 0) Click(DeltaX > 0 ? 64 : 32, std::abs(DeltaX) / 120);
		break;
	}
	case EInput::Key:
	{
		const uint8_t Type = R.U8();
		const int32_t KeyCode = int32_t(R.U32());
		R.U32();
		uint16_t Character = 0;
		R.Read(&Character, 2);
		const uint32_t Modifiers = R.U32();
		if (!R.Ok) break;
		if (Type == uint8_t(TSHostProtocol::EKeyType::Char))
		{
			uint32_t Keysym = 0;
			if (Character >= 0x20 && Character != 0x7F)
			{
				Keysym = KeysymForChar(Character);
			}
			else if ((Modifiers & TSHostProtocol::ModControl) && Character >= 1 && Character <= 26)
			{
				// Ctrl+letter arrives as a control character; send the letter while Ctrl is held.
				Keysym = 'a' + Character - 1;
			}
			if (Keysym)
			{
				SendKey(Keysym, true);
				SendKey(Keysym, false);
			}
		}
		else if (const uint32_t Keysym = KeysymForVirtualKey(KeyCode))
		{
			SendKey(Keysym, Type != uint8_t(TSHostProtocol::EKeyType::KeyUp));
		}
		break;
	}
	case EInput::Text:
	{
		const std::wstring Text = Widen(R.Str());
		if (!R.Ok) break;
		for (wchar_t Char : Text)
		{
			const uint32_t Keysym = Char == L'\n' ? 0xff0d : KeysymForChar(uint32_t(Char));
			SendKey(Keysym, true);
			SendKey(Keysym, false);
		}
		break;
	}
	default:
		break;
	}
}
