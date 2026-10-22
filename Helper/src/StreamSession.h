#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"
#include "StreamCodec.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// Packet kinds in StreamPacket messages.
enum class EStreamKind : uint8_t
{
	Video = 0,
	Audio = 1,
};

constexpr uint8_t StreamFlagKey = 1;

struct FStreamPacket
{
	EStreamKind Kind = EStreamKind::Video;
	uint8_t Flags = 0;
	uint32_t PtsMs = 0;
	uint32_t Codec = 0;
	std::vector<uint8_t> Data;
};

struct FSenderParams
{
	uint32_t SessionId = 0;
	uint32_t SourceSessionId = 0;
	int Fps = 30;
	int BitrateKbps = 2500;
	int AudioKbps = 96;
	bool bAllowSoftware = false;
	std::string Prefer;
};

// Encodes another session's frames and audio for a live stream and hands the packets to the mod.
class FStreamSenderSession : public FSession
{
public:
	using FOnPacket = std::function<void(uint32_t Session, const FStreamPacket& Packet)>;

	FStreamSenderSession(const FSenderParams& Params, FSessionEvents Events, FOnPacket OnPacket);
	virtual ~FStreamSenderSession() override;

	virtual void HandleStreamControl(uint8_t Command, uint32_t Value) override;

private:
	void RunVideo();
	void EncodeAudio(const float* Interleaved, int Frames);

	FSenderParams Params;
	FSessionEvents Events;
	FOnPacket OnPacket;
	int VideoTap = -1, AudioTap = -1;
	std::atomic<bool> Stopping{false};
	std::atomic<bool> KeyframeWanted{true};
	std::atomic<int> BitrateKbps{2500};
	std::thread VideoThread;
	double StartedAt = 0.0;

	std::mutex FrameMutex;
	std::vector<uint8_t> Latest;
	int LatestWidth = 0, LatestHeight = 0;
	bool bFresh = false;

	std::mutex AudioMutex;
	AVCodecContext* AudioCodec = nullptr;
	AVFrame* AudioFrame = nullptr;
	AVPacket* AudioPacket = nullptr;
	std::vector<float> AudioFifo;
	int64_t AudioSamplesSent = 0;
};

// Decodes a live stream's packets from the mod into this session's video buffer and audio ring.
class FStreamReceiverSession : public FSession
{
public:
	FStreamReceiverSession(uint32_t SessionId, FSessionEvents Events);
	virtual ~FStreamReceiverSession() override;

	virtual void HandleStreamPacket(const FStreamPacket& Packet) override;
	virtual void HandleStreamControl(uint8_t Command, uint32_t Value) override;

private:
	struct FFrame
	{
		uint32_t PtsMs;
		int Width, Height;
		std::vector<uint8_t> Bgra;
	};

	void RunVideo();
	void RunPresenter();
	double AudioClockMs();

	uint32_t SessionId;
	FSessionEvents Events;
	std::atomic<bool> Stopping{false};
	std::thread VideoThread, PresenterThread;

	std::mutex PacketMutex;
	std::condition_variable PacketCv;
	std::deque<FStreamPacket> VideoPackets;
	std::atomic<bool> WaitForKeyframe{true};

	std::mutex FrameMutex;
	std::deque<FFrame> Frames;

	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;

	std::mutex AudioMutex;
	AVCodecContext* AudioDecoder = nullptr;
	AVFrame* AudioFrame = nullptr;
	AVPacket* AudioPkt = nullptr;
	std::unique_ptr<FAudioWriter> AudioWriter;
	std::deque<std::pair<int64_t, uint32_t>> AudioMap;
	std::vector<float> Interleaved;
	std::atomic<double> LastAudioAt{0.0};

	// Statistics for the state message.
	std::atomic<uint64_t> BytesIn{0};
	std::atomic<int> FramesShown{0};
	std::string CodecName;
};
