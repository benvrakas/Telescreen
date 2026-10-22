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

struct FWindowParams
{
	uint32_t SessionId = 0;
	uint64_t Window = 0;
	bool bCursor = true;
	bool bAudio = true;
	// Frames larger than this are scaled down to fit (keeping the shape).
	uint32_t MaxWidth = 1920;
	uint32_t MaxHeight = 1080;
};

// A window on this PC that can be shared.
struct FWindowInfo
{
	uint64_t Window = 0;
	uint32_t ProcessId = 0;
	std::string Title;
	std::string Exe;
};

// Lists top-level windows worth sharing: visible, titled, not cloaked, not tool windows, not ExcludeProcess's.
std::vector<FWindowInfo> ListShareableWindows(uint32_t ExcludeProcess);

// Shares one window: Windows Graphics Capture frames into the session's video buffer and the frame taps, the window
// process's own sound (process loopback) into the audio taps only (the controller already hears the window), and
// input posted to the window (best effort; apps reading raw input ignore it).
class FWindowSession : public FSession
{
public:
	FWindowSession(const FWindowParams& InParams, FSessionEvents InEvents);
	virtual ~FWindowSession() override;

	virtual void HandleInput(TSHostProtocol::EInput Kind, FPayloadReader& Reader) override;

private:
	struct FCapture;

	void RunCapture();
	void RunAudio();
	void RunWatch();
	void OnFrame(const uint8_t* Bgra, int RowPitch, int Width, int Height);
	void Publish();
	void PostMouse(UINT Message, int X, int Y, WPARAM Buttons);
	void SetState(TSHostProtocol::ESessionState State, const std::string& Message);

	FWindowParams Params;
	FSessionEvents Events;
	HWND Window = nullptr;
	uint32_t ProcessId = 0;

	std::mutex FrameMutex;
	struct SwsContext* Scaler = nullptr;
	std::vector<uint8_t> View;
	int ViewWidth = 0, ViewHeight = 0;
	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;
	double LastPublish = 0.0;

	std::atomic<bool> Stopping{false};
	std::thread CaptureThread;
	std::thread AudioThread;
	std::thread WatchThread;
	HANDLE StopEvent = nullptr;

	std::mutex StateMutex;
	TSHostProtocol::ESessionState LastState = TSHostProtocol::ESessionState::Resolving;
	std::string LastMessage;
};
