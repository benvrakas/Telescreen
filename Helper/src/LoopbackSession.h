#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"
#include "StreamCodec.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// Lets other sessions receive a session's published frames (BGRA, Width x Height).
namespace FrameTaps
{
	using FTap = std::function<void(const uint8_t* Bgra, int Width, int Height)>;

	int Add(uint32_t SourceSession, FTap Tap);
	void Remove(int TapId);
	void Publish(uint32_t SourceSession, const uint8_t* Bgra, int Width, int Height);
}

struct FLoopbackParams
{
	uint32_t SessionId = 0;
	uint32_t SourceSessionId = 0;
	int Fps = 30;
	int BitrateKbps = 3000;
	std::string Prefer;
};

// M3 test: encodes another session's frames with the stream encoder and decodes them again into this session's
// video buffer, reporting encode/decode times and bitrate in its state message.
class FLoopbackSession : public FSession
{
public:
	FLoopbackSession(const FLoopbackParams& Params, FSessionEvents Events);
	virtual ~FLoopbackSession() override;

private:
	void Run();

	FLoopbackParams Params;
	FSessionEvents Events;
	int TapId = -1;
	std::atomic<bool> Stopping{false};
	std::thread Worker;

	std::mutex FrameMutex;
	std::vector<uint8_t> Latest;
	int LatestWidth = 0, LatestHeight = 0;
	bool bFresh = false;
};
