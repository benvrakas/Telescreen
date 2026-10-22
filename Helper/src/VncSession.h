#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct FVncParams
{
	uint32_t SessionId = 0;
	std::string Host;
	uint16_t Port = 5900;
	std::string Password;
	// Frames larger than this are scaled down to fit (keeping the shape).
	uint32_t MaxWidth = 1920;
	uint32_t MaxHeight = 1080;
	bool bAudio = true;
};

// A VNC (RFB) client: connects to a VNC server (QEMU/Proxmox consoles, TigerVNC, x11vnc...), shows its desktop in the
// session's video buffer and the frame taps, plays QEMU's audio when the server offers it, and sends the controller's
// mouse and keys over the protocol. Supports RFB 3.3/3.7/3.8, security None and VNC Authentication, encodings Raw,
// CopyRect, Hextile, ZRLE, DesktopSize, Cursor and LastRect. See Docs/RemoteDesktop.md.
class FVncSession : public FSession
{
public:
	FVncSession(const FVncParams& InParams, FSessionEvents InEvents);
	virtual ~FVncSession() override;

	virtual void HandleInput(TSHostProtocol::EInput Kind, FPayloadReader& Reader) override;

private:
	void Run();
	bool Connect(std::string& Error);
	bool Handshake(std::string& Error);
	bool ReadExact(void* Data, size_t Size);
	bool Skip(size_t Size);
	bool Send(const void* Data, size_t Size);
	bool HandleFramebufferUpdate();
	bool DecodeRaw(int X, int Y, int W, int H);
	bool DecodeCopyRect(int X, int Y, int W, int H);
	bool DecodeHextile(int X, int Y, int W, int H);
	bool DecodeZrle(int X, int Y, int W, int H);
	bool DecodeCursor(int HotX, int HotY, int W, int H);
	bool HandleQemuMessage();
	void Resize(int W, int H);
	void Fill(int X, int Y, int W, int H, uint32_t Pixel);
	void RequestUpdate(bool bIncremental);
	void Publish();
	void PublishLocked();
	void SendPointer();
	void SendKey(uint32_t Keysym, bool bDown);
	void SetState(TSHostProtocol::ESessionState State, const std::string& Message);

	FVncParams Params;
	FSessionEvents Events;
	uintptr_t Socket = ~uintptr_t(0);
	std::mutex SendMutex;
	std::atomic<bool> Stopping{false};
	std::thread Worker;

	// The remote framebuffer as BGRX, and its size.
	std::mutex FrameMutex;
	std::vector<uint32_t> Framebuffer;
	int FbWidth = 0, FbHeight = 0;
	bool bDirty = false;
	double LastPublish = 0.0;
	std::vector<uint8_t> View;
	int ViewWidth = 0, ViewHeight = 0;
	struct SwsContext* Scaler = nullptr;
	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;

	// The server's pointer image (Cursor pseudo-encoding), drawn at the pointer position.
	std::vector<uint32_t> CursorPixels;
	std::vector<uint8_t> CursorMask;
	int CursorWidth = 0, CursorHeight = 0, CursorHotX = 0, CursorHotY = 0;

	// Pointer state sent with every PointerEvent (framebuffer pixels, RFB button mask).
	std::atomic<int> PointerX{0}, PointerY{0};
	std::atomic<uint8_t> Buttons{0};

	// ZRLE's zlib stream lasts for the whole connection.
	void* Zrle = nullptr;
	std::vector<uint8_t> ZrleIn, ZrleOut;
	// Why decoding stopped, if it did (shown instead of "the server closed the connection").
	std::string DecodeError;

	// QEMU audio.
	bool bAudioOn = false;
	std::unique_ptr<FAudioWriter> AudioWriter;
	std::vector<float> AudioFloats;

	std::mutex StateMutex;
	TSHostProtocol::ESessionState LastState = TSHostProtocol::ESessionState::Resolving;
	std::string LastMessage;
};
