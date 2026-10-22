#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
struct SwrContext;

struct FOpenParams
{
	uint32_t SessionId = 0;
	std::string Url;
	bool IsLocalFile = false;
	double StartSeconds = 0.0;
	bool Paused = false;
	uint32_t MaxWidth = 1280;
	uint32_t MaxHeight = 720;
};

// Plays one media URL or file into a session's shared video and audio buffers, following the mod's sync target.
class FMediaSession : public FSession
{
public:
	FMediaSession(const FOpenParams& Params, const std::wstring& YtDlpPath, FSessionEvents Events);
	virtual ~FMediaSession() override;

	// Where playback should be now, in media seconds; thread-safe.
	virtual void SetTarget(double MediaSeconds, bool Paused) override;

	uint64_t GetFramesPublished() const { return FramesPublished.load(); }

private:
	struct FInput;
	struct FPacketQueue;
	struct FVideoFrame
	{
		double Pts = 0.0;
		uint32_t Generation = 0;
		std::vector<uint8_t> Bgra;
	};

	void Run();
	bool Open(std::string& Error);
	void Close();
	void DemuxLoop(FInput* Input);
	void VideoDecodeLoop();
	void AudioDecodeLoop();
	void PresentLoop();

	void RequestSeek(double Target);
	double ComputeClock(double Now);
	void ControlSync(double Now, double Clock);
	void PublishDueFrames(double Clock);
	bool EnsureVideoWriter(uint32_t Width, uint32_t Height);
	void SendState(TSHostProtocol::ESessionState State, double Position, const std::string& Message = std::string());
	double PtsForRingFrame(int64_t Frame);

	FOpenParams Params;
	std::wstring YtDlpPath;
	FSessionEvents Events;

	std::atomic<bool> Stopping{false};
	std::thread SessionThread;
	std::vector<std::unique_ptr<FInput>> Inputs;
	std::unique_ptr<FPacketQueue> VideoPackets;
	std::unique_ptr<FPacketQueue> AudioPackets;
	std::thread VideoThread, AudioThread;

	AVCodecContext* VideoCodec = nullptr;
	AVCodecContext* AudioCodec = nullptr;
	double VideoTimeBase = 0.0, AudioTimeBase = 0.0;
	double VideoStartOffset = 0.0, AudioStartOffset = 0.0;
	double Duration = 0.0;
	bool IsLive = false;
	std::string Title;

	// Seeking: every packet and frame carries the generation it was produced in; stale ones are dropped.
	std::atomic<uint32_t> SeekGeneration{0};
	std::atomic<double> SeekTarget{0.0};

	// Decoded video waiting for its presentation time.
	std::mutex FrameMutex;
	std::condition_variable FrameCv;
	std::deque<FVideoFrame> Frames;
	std::vector<std::vector<uint8_t>> FramePool;
	uint32_t OutWidth = 0, OutHeight = 0;
	SwsContext* Sws = nullptr;

	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;
	std::unique_ptr<FAudioWriter> AudioWriter;
	SwrContext* Swr = nullptr;
	std::atomic<int32_t> CompensationDelta{0};
	std::atomic<bool> CompensationPending{false};

	// Ring frame index -> media time of the audio written there, one entry per written chunk.
	std::mutex AudioMapMutex;
	std::deque<std::pair<int64_t, double>> AudioMap;

	// Target set by the mod.
	std::mutex TargetMutex;
	double TargetMedia = 0.0;
	double TargetReceivedAt = 0.0;
	bool TargetPaused = false;
	bool TargetValid = false;

	// Presenter-owned clock state.
	std::atomic<bool> Paused{false};
	std::atomic<bool> Holding{true};
	// Generation for which the audio thread has converted audio waiting to be written.
	std::atomic<uint32_t> AudioReadyGeneration{UINT32_MAX};
	double SeekStartedAt = 0.0;
	double LastSeekLatency = 0.3;
	double HoldPosition = 0.0;
	std::atomic<bool> WallClock{false};
	double WallBase = 0.0, WallStart = 0.0, WallRate = 1.0;
	int64_t LastReadFrames = -1;
	double LastReadChange = 0.0;
	double LastClock = 0.0;
	double NextSyncCheck = 0.0, NextStateReport = 0.0, NextCompensation = 0.0;
	std::atomic<uint64_t> FramesPublished{0};
	bool Ended = false;
};
