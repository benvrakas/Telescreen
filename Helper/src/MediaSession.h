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
struct AVStream;
struct AVFrame;
struct SwsContext;
struct SwrContext;
struct FResolvedMedia;

struct FOpenParams
{
	uint32_t SessionId = 0;
	std::string Url;
	bool IsLocalFile = false;
	double StartSeconds = 0.0;
	bool Paused = false;
	uint32_t MaxWidth = 1280;
	uint32_t MaxHeight = 720;
	// Preferred subtitle language (e.g. "en"): which automatic captions are offered.
	std::string SubtitleLanguage;
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

	// SelectSubtitles: loads the track's cues and sends them (embedded tracks: as the file plays).
	virtual void HandleCommand(TSHostProtocol::ECommand Command, FPayloadReader& Reader) override;

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
	void PublishTapAudio();
	void FindSubtitleTracks(const FResolvedMedia* Resolved);
	void DecodeSubtitlePacket(FInput* Input, AVPacket* Packet);
	void DecodePictureSubtitle(FInput* Input, AVStream* Stream, AVPacket* Packet);
	void BlendPictureSubtitles(uint8_t* Bgra, int Width, int Height, double Pts);
	bool ReadEmbeddedSubtitles(int StreamIndex, std::vector<FSubtitleCue>& Cues, std::string& Error);
	static bool DecodeSubtitle(AVCodecContext* Codec, AVStream* Stream, double StartOffset, AVPacket* Packet, FSubtitleCue& Cue);

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

	// For a live stream of this video (players without the file): the shown frame (kept only while tapped) and the
	// audio written to the ring, handed to the taps as the game plays it so it stays in step with the picture.
	std::mutex TapMutex;
	std::vector<uint8_t> TapFrame;
	int TapWidth = 0, TapHeight = 0;
	std::vector<float> TapAudio;
	int64_t TapAudioStart = 0;

	// Subtitles: the tracks found at open; the selected embedded stream is decoded by the first input's demux thread.
	std::mutex SubtitleMutex;
	std::vector<FSubtitleTrack> SubtitleTracks;
	std::atomic<int> SelectedSubtitleStream{-1};
	std::atomic<uint32_t> SelectedSubtitleTrack{0};
	AVCodecContext* SubtitleCodec = nullptr;
	int SubtitleCodecStream = -1;
	std::thread SubtitleLoader;
	std::atomic<uint32_t> SubtitleRequest{0};

	// Picture subtitles of the selected picture track, scaled to the output size as they are decoded (the demuxer
	// reads ahead of playback, so they arrive before they show).
	struct FPictureCue
	{
		int64_t StartMs = 0, EndMs = 0;
		int X = 0, Y = 0, Width = 0, Height = 0;
		std::vector<uint8_t> Bgra;
	};
	std::atomic<bool> bPictureTrack{false};
	// The demuxer reads well ahead of playback: a newly picked picture track re-reads from where playback is.
	std::atomic<bool> PictureSeekPending{false};
	std::mutex PictureMutex;
	std::deque<FPictureCue> PictureCues;
	SwsContext* PictureScaler = nullptr;
	int PictureCount = 0, PictureFrames = 0;
};
