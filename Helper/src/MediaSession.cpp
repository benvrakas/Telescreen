#include "MediaSession.h"
#include "YtDlp.h"

#include <algorithm>
#include <cmath>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

using TSHostProtocol::ESessionState;

namespace
{
	constexpr uint32_t OutSampleRate = 48000;
	constexpr uint32_t OutChannels = 2;
	constexpr uint32_t RingCapacityFrames = OutSampleRate;            // 1 s
	constexpr int64_t AudioAheadFrames = OutSampleRate * 3 / 10;       // write at most 0.3 s ahead of the reader
	constexpr size_t MaxQueuedFrames = 5;
	constexpr size_t MaxQueuedPacketBytes = 16u << 20;
	constexpr double SeekThreshold = 1.5;
	constexpr double DriftTolerance = 0.04;
	constexpr double MaxCompensation = 0.05;
	constexpr double ConsumerIdleSeconds = 1.0;

	std::string AvError(int Code)
	{
		char Buffer[256];
		av_strerror(Code, Buffer, sizeof(Buffer));
		return Buffer;
	}

	int InterruptCallback(void* Opaque)
	{
		return static_cast<std::atomic<bool>*>(Opaque)->load() ? 1 : 0;
	}
}

struct FMediaSession::FPacketQueue
{
	std::mutex Mutex;
	std::condition_variable Cv;
	// Null packet = flush marker for its generation.
	std::deque<std::pair<AVPacket*, uint32_t>> Items;
	size_t Bytes = 0;
	bool Eof = false;

	~FPacketQueue() { Clear(); }

	void Clear()
	{
		for (auto& Item : Items) av_packet_free(&Item.first);
		Items.clear();
		Bytes = 0;
	}
};

struct FMediaSession::FInput
{
	AVFormatContext* Format = nullptr;
	int VideoStream = -1;
	int AudioStream = -1;
	double StartOffset = 0.0;
	std::thread Thread;
};

FMediaSession::FMediaSession(const FOpenParams& InParams, const std::wstring& InYtDlpPath, FSessionEvents InEvents)
	: Params(InParams), YtDlpPath(InYtDlpPath), Events(std::move(InEvents))
{
	VideoPackets = std::make_unique<FPacketQueue>();
	AudioPackets = std::make_unique<FPacketQueue>();
	TargetMedia = Params.StartSeconds;
	TargetPaused = Params.Paused;
	TargetReceivedAt = NowSeconds();
	TargetValid = true;
	SessionThread = std::thread([this] { Run(); });
}

FMediaSession::~FMediaSession()
{
	Stopping = true;
	for (FPacketQueue* Queue : {VideoPackets.get(), AudioPackets.get()}) Queue->Cv.notify_all();
	FrameCv.notify_all();
	if (SessionThread.joinable()) SessionThread.join();
	LOG_INFO("session %u closed", Params.SessionId);
}

void FMediaSession::SetTarget(double MediaSeconds, bool InPaused)
{
	std::lock_guard<std::mutex> Lock(TargetMutex);
	TargetMedia = MediaSeconds;
	TargetPaused = InPaused;
	TargetReceivedAt = NowSeconds();
	TargetValid = true;
}

void FMediaSession::SendState(ESessionState State, double Position, const std::string& Message)
{
	if (Events.OnState) Events.OnState(Params.SessionId, State, Position, Duration, Message);
}

void FMediaSession::Run()
{
	LOG_INFO("session %u: opening %s (start %.2f, %s)", Params.SessionId, Params.Url.c_str(), Params.StartSeconds,
		Params.Paused ? "paused" : "playing");
	SendState(ESessionState::Resolving, Params.StartSeconds);
	std::string Error;
	if (!Open(Error))
	{
		LOG_ERROR("session %u: %s", Params.SessionId, Error.c_str());
		SendState(ESessionState::Error, 0.0, Error);
		Close();
		return;
	}
	PresentLoop();
	Close();
}

bool FMediaSession::Open(std::string& Error)
{
	std::vector<std::string> Urls;
	if (Params.IsLocalFile)
	{
		Urls.push_back(Params.Url);
		const size_t Slash = Params.Url.find_last_of("/\\");
		Title = Slash == std::string::npos ? Params.Url : Params.Url.substr(Slash + 1);
	}
	else
	{
		const FResolvedMedia Resolved = ResolveWithYtDlp(YtDlpPath, Params.Url, true, Params.MaxHeight);
		if (!Resolved.Ok)
		{
			Error = Resolved.Error;
			return false;
		}
		Urls = Resolved.StreamUrls;
		Title = Resolved.Title;
		Duration = Resolved.Duration;
		IsLive = Resolved.IsLive;
		LOG_INFO("session %u: \"%s\", %.0f s%s, %zu stream URL(s)", Params.SessionId, Title.c_str(), Duration, IsLive ? ", live" : "",
			Urls.size());
	}
	if (Stopping) return false;

	for (const std::string& Url : Urls)
	{
		auto Input = std::make_unique<FInput>();
		Input->Format = avformat_alloc_context();
		Input->Format->interrupt_callback.callback = InterruptCallback;
		Input->Format->interrupt_callback.opaque = &Stopping;
		AVDictionary* Options = nullptr;
		if (!Params.IsLocalFile)
		{
			av_dict_set(&Options, "reconnect", "1", 0);
			av_dict_set(&Options, "reconnect_streamed", "1", 0);
			av_dict_set(&Options, "reconnect_on_network_error", "1", 0);
			av_dict_set(&Options, "reconnect_delay_max", "5", 0);
			av_dict_set(&Options, "rw_timeout", "15000000", 0);
			// Open-ended range requests to YouTube are throttled to about real time; 4 MB ranges are not.
			av_dict_set(&Options, "request_size", "4194304", 0);
			av_dict_set(&Options, "initial_request_size", "1048576", 0);
			av_dict_set(&Options, "multiple_requests", "1", 0);
		}
		const int Code = avformat_open_input(&Input->Format, Url.c_str(), nullptr, &Options);
		av_dict_free(&Options);
		if (Code < 0)
		{
			Error = "could not open stream: " + AvError(Code);
			return false;
		}
		avformat_find_stream_info(Input->Format, nullptr);
		Input->StartOffset = Input->Format->start_time != AV_NOPTS_VALUE ? Input->Format->start_time / double(AV_TIME_BASE) : 0.0;
		if (Duration <= 0.0 && Input->Format->duration > 0) Duration = Input->Format->duration / double(AV_TIME_BASE);
		Inputs.push_back(std::move(Input));
	}

	// Pick the first video and audio stream across the inputs and open their decoders.
	for (auto& Input : Inputs)
	{
		for (int Kind : {AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO})
		{
			AVCodecContext*& Codec = Kind == AVMEDIA_TYPE_VIDEO ? VideoCodec : AudioCodec;
			if (Codec) continue;
			const AVCodec* Decoder = nullptr;
			const int Index = av_find_best_stream(Input->Format, AVMediaType(Kind), -1, -1, &Decoder, 0);
			if (Index < 0 || !Decoder) continue;
			AVStream* Stream = Input->Format->streams[Index];
			Codec = avcodec_alloc_context3(Decoder);
			avcodec_parameters_to_context(Codec, Stream->codecpar);
			Codec->thread_count = 0;
			const int Code = avcodec_open2(Codec, Decoder, nullptr);
			if (Code < 0)
			{
				LOG_WARN("session %u: could not open %s decoder %s: %s", Params.SessionId, Kind == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
					Decoder->name, AvError(Code).c_str());
				avcodec_free_context(&Codec);
				continue;
			}
			const double TimeBase = av_q2d(Stream->time_base);
			if (Kind == AVMEDIA_TYPE_VIDEO)
			{
				Input->VideoStream = Index;
				VideoTimeBase = TimeBase;
				VideoStartOffset = Input->StartOffset;
			}
			else
			{
				Input->AudioStream = Index;
				AudioTimeBase = TimeBase;
				AudioStartOffset = Input->StartOffset;
			}
			LOG_INFO("session %u: %s stream %d uses %s (%dx%d, %d Hz)", Params.SessionId, Kind == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
				Index, Decoder->name, Codec->width, Codec->height, Codec->sample_rate);
		}
	}
	if (!VideoCodec && !AudioCodec)
	{
		Error = "no playable video or audio stream";
		return false;
	}

	if (AudioCodec)
	{
		AudioWriter = std::make_unique<FAudioWriter>();
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-audio";
		if (!AudioWriter->Create(Name, OutSampleRate, OutChannels, RingCapacityFrames))
		{
			Error = "could not create the audio buffer";
			return false;
		}
		if (Events.OnAudio) Events.OnAudio(Params.SessionId, OutSampleRate, OutChannels, RingCapacityFrames, Name);
	}
	else
	{
		WallClock = true;
	}

	Paused = TargetPaused;
	HoldPosition = Params.StartSeconds;
	SeekStartedAt = NowSeconds();
	if (Params.StartSeconds > 0.5 && !IsLive)
	{
		RequestSeek(Params.StartSeconds);
	}
	for (auto& Input : Inputs)
	{
		FInput* Raw = Input.get();
		Input->Thread = std::thread([this, Raw] { DemuxLoop(Raw); });
	}
	if (VideoCodec) VideoThread = std::thread([this] { VideoDecodeLoop(); });
	if (AudioCodec) AudioThread = std::thread([this] { AudioDecodeLoop(); });
	return true;
}

void FMediaSession::Close()
{
	Stopping = true;
	for (FPacketQueue* Queue : {VideoPackets.get(), AudioPackets.get()}) Queue->Cv.notify_all();
	FrameCv.notify_all();
	for (auto& Input : Inputs)
	{
		if (Input->Thread.joinable()) Input->Thread.join();
	}
	if (VideoThread.joinable()) VideoThread.join();
	if (AudioThread.joinable()) AudioThread.join();
	for (auto& Input : Inputs) avformat_close_input(&Input->Format);
	Inputs.clear();
	avcodec_free_context(&VideoCodec);
	avcodec_free_context(&AudioCodec);
	sws_freeContext(Sws);
	Sws = nullptr;
	swr_free(&Swr);
}

void FMediaSession::RequestSeek(double Target)
{
	Target = std::max(0.0, Duration > 0.0 ? std::min(Target, Duration - 0.5) : Target);
	SeekTarget = Target;
	const uint32_t Generation = ++SeekGeneration;
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		for (FVideoFrame& Frame : Frames) FramePool.push_back(std::move(Frame.Bgra));
		Frames.clear();
	}
	FrameCv.notify_all();
	if (AudioWriter) AudioWriter->DiscardQueued();
	{
		std::lock_guard<std::mutex> Lock(AudioMapMutex);
		AudioMap.clear();
	}
	Holding = true;
	HoldPosition = Target;
	SeekStartedAt = NowSeconds();
	LastReadFrames = -1;
	Ended = false;
	LOG_INFO("session %u: seek to %.2f (generation %u)", Params.SessionId, Target, Generation);
}

void FMediaSession::DemuxLoop(FInput* Input)
{
	uint32_t Generation = 0;
	AVPacket* Packet = av_packet_alloc();
	bool Eof = false;
	while (!Stopping)
	{
		const uint32_t Wanted = SeekGeneration.load();
		if (Wanted != Generation)
		{
			const double Target = SeekTarget.load() + Input->StartOffset;
			const int Code = av_seek_frame(Input->Format, -1, int64_t(Target * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
			if (Code < 0) LOG_WARN("session %u: seek failed: %s", Params.SessionId, AvError(Code).c_str());
			for (FPacketQueue* Queue : {VideoPackets.get(), AudioPackets.get()})
			{
				if ((Queue == VideoPackets.get() && Input->VideoStream < 0) || (Queue == AudioPackets.get() && Input->AudioStream < 0)) continue;
				std::lock_guard<std::mutex> Lock(Queue->Mutex);
				Queue->Clear();
				Queue->Items.push_back({nullptr, Wanted});
				Queue->Eof = false;
				Queue->Cv.notify_all();
			}
			Generation = Wanted;
			Eof = false;
		}
		if (Eof)
		{
			Sleep(20);
			continue;
		}

		const int Code = av_read_frame(Input->Format, Packet);
		if (Code < 0)
		{
			if (Code != AVERROR_EXIT) LOG_INFO("session %u: input ended (%s)", Params.SessionId, AvError(Code).c_str());
			for (FPacketQueue* Queue : {VideoPackets.get(), AudioPackets.get()})
			{
				if ((Queue == VideoPackets.get() && Input->VideoStream < 0) || (Queue == AudioPackets.get() && Input->AudioStream < 0)) continue;
				std::lock_guard<std::mutex> Lock(Queue->Mutex);
				Queue->Eof = true;
				Queue->Cv.notify_all();
			}
			Eof = true;
			continue;
		}
		FPacketQueue* Queue = Packet->stream_index == Input->VideoStream ? VideoPackets.get()
			: Packet->stream_index == Input->AudioStream                 ? AudioPackets.get()
																		 : nullptr;
		if (!Queue)
		{
			av_packet_unref(Packet);
			continue;
		}
		std::unique_lock<std::mutex> Lock(Queue->Mutex);
		Queue->Cv.wait(Lock, [&] { return Stopping || Queue->Bytes < MaxQueuedPacketBytes || SeekGeneration.load() != Generation; });
		if (Stopping || SeekGeneration.load() != Generation)
		{
			av_packet_unref(Packet);
			continue;
		}
		Queue->Bytes += Packet->size;
		Queue->Items.push_back({av_packet_clone(Packet), Generation});
		Queue->Cv.notify_all();
		av_packet_unref(Packet);
	}
	av_packet_free(&Packet);
}

void FMediaSession::VideoDecodeLoop()
{
	AVFrame* Frame = av_frame_alloc();
	uint32_t Generation = 0;
	while (!Stopping)
	{
		AVPacket* Packet = nullptr;
		uint32_t PacketGeneration = 0;
		{
			std::unique_lock<std::mutex> Lock(VideoPackets->Mutex);
			VideoPackets->Cv.wait(Lock, [&] { return Stopping || !VideoPackets->Items.empty(); });
			if (Stopping) break;
			Packet = VideoPackets->Items.front().first;
			PacketGeneration = VideoPackets->Items.front().second;
			VideoPackets->Items.pop_front();
			if (Packet) VideoPackets->Bytes -= Packet->size;
			VideoPackets->Cv.notify_all();
		}
		if (PacketGeneration != SeekGeneration.load())
		{
			av_packet_free(&Packet);
			continue;
		}
		if (!Packet)
		{
			avcodec_flush_buffers(VideoCodec);
			Generation = PacketGeneration;
			continue;
		}
		avcodec_send_packet(VideoCodec, Packet);
		av_packet_free(&Packet);
		while (!Stopping && avcodec_receive_frame(VideoCodec, Frame) == 0)
		{
			const int64_t Ts = Frame->best_effort_timestamp != AV_NOPTS_VALUE ? Frame->best_effort_timestamp : Frame->pts;
			const double Pts = Ts == AV_NOPTS_VALUE ? 0.0 : Ts * VideoTimeBase - VideoStartOffset;
			if (Generation != SeekGeneration.load() || Pts < SeekTarget.load() - 0.05)
			{
				av_frame_unref(Frame);
				continue;
			}

			// Fit inside MaxWidth x MaxHeight, keeping the aspect ratio, never upscaling.
			const double Sar = Frame->sample_aspect_ratio.num > 0 ? av_q2d(Frame->sample_aspect_ratio) : 1.0;
			const double SourceWidth = Frame->width * Sar;
			const double Scale = std::min({1.0, Params.MaxWidth / SourceWidth, Params.MaxHeight / double(Frame->height)});
			const uint32_t Width = std::max(2u, uint32_t(SourceWidth * Scale) & ~1u);
			const uint32_t Height = std::max(2u, uint32_t(Frame->height * Scale) & ~1u);
			Sws = sws_getCachedContext(Sws, Frame->width, Frame->height, AVPixelFormat(Frame->format), Width, Height, AV_PIX_FMT_BGRA,
				SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (!Sws)
			{
				LOG_ERROR("session %u: no scaler for %dx%d format %d", Params.SessionId, Frame->width, Frame->height, Frame->format);
				av_frame_unref(Frame);
				continue;
			}

			FVideoFrame Out;
			Out.Pts = Pts;
			Out.Generation = Generation;
			{
				std::lock_guard<std::mutex> Lock(FrameMutex);
				if (!FramePool.empty())
				{
					Out.Bgra = std::move(FramePool.back());
					FramePool.pop_back();
				}
			}
			Out.Bgra.resize(size_t(Width) * Height * 4);
			uint8_t* Dst[4] = {Out.Bgra.data(), nullptr, nullptr, nullptr};
			int DstStride[4] = {int(Width * 4), 0, 0, 0};
			sws_scale(Sws, Frame->data, Frame->linesize, 0, Frame->height, Dst, DstStride);
			av_frame_unref(Frame);

			std::unique_lock<std::mutex> Lock(FrameMutex);
			FrameCv.wait(Lock, [&] { return Stopping || Frames.size() < MaxQueuedFrames || Generation != SeekGeneration.load(); });
			if (Stopping || Generation != SeekGeneration.load())
			{
				FramePool.push_back(std::move(Out.Bgra));
				continue;
			}
			OutWidth = Width;
			OutHeight = Height;
			Frames.push_back(std::move(Out));
		}
	}
	av_frame_free(&Frame);
}

void FMediaSession::AudioDecodeLoop()
{
	AVFrame* Frame = av_frame_alloc();
	uint32_t Generation = 0;
	std::vector<float> Converted;
	while (!Stopping)
	{
		AVPacket* Packet = nullptr;
		uint32_t PacketGeneration = 0;
		{
			std::unique_lock<std::mutex> Lock(AudioPackets->Mutex);
			AudioPackets->Cv.wait(Lock, [&] { return Stopping || !AudioPackets->Items.empty(); });
			if (Stopping) break;
			Packet = AudioPackets->Items.front().first;
			PacketGeneration = AudioPackets->Items.front().second;
			AudioPackets->Items.pop_front();
			if (Packet) AudioPackets->Bytes -= Packet->size;
			AudioPackets->Cv.notify_all();
		}
		if (PacketGeneration != SeekGeneration.load())
		{
			av_packet_free(&Packet);
			continue;
		}
		if (!Packet)
		{
			avcodec_flush_buffers(AudioCodec);
			if (Swr) swr_init(Swr);
			Generation = PacketGeneration;
			continue;
		}
		avcodec_send_packet(AudioCodec, Packet);
		av_packet_free(&Packet);
		while (!Stopping && avcodec_receive_frame(AudioCodec, Frame) == 0)
		{
			if (!Swr)
			{
				AVChannelLayout OutLayout;
				av_channel_layout_default(&OutLayout, OutChannels);
				if (swr_alloc_set_opts2(&Swr, &OutLayout, AV_SAMPLE_FMT_FLT, OutSampleRate, &Frame->ch_layout, AVSampleFormat(Frame->format),
						Frame->sample_rate, 0, nullptr) < 0 ||
					swr_init(Swr) < 0)
				{
					LOG_ERROR("session %u: could not set up audio conversion", Params.SessionId);
					swr_free(&Swr);
					av_frame_unref(Frame);
					continue;
				}
			}
			if (CompensationPending.exchange(false))
			{
				swr_set_compensation(Swr, CompensationDelta.load(), int(OutSampleRate));
			}

			const int64_t Ts = Frame->best_effort_timestamp != AV_NOPTS_VALUE ? Frame->best_effort_timestamp : Frame->pts;
			double Pts = Ts == AV_NOPTS_VALUE ? 0.0 : Ts * AudioTimeBase - AudioStartOffset;
			const int MaxOut = swr_get_out_samples(Swr, Frame->nb_samples) + 256;
			Converted.resize(size_t(MaxOut) * OutChannels);
			uint8_t* OutPlanes[1] = {reinterpret_cast<uint8_t*>(Converted.data())};
			int Count = swr_convert(Swr, OutPlanes, MaxOut, const_cast<const uint8_t**>(Frame->extended_data), Frame->nb_samples);
			av_frame_unref(Frame);
			if (Count <= 0 || Generation != SeekGeneration.load()) continue;

			// After a seek, drop audio before the target.
			const float* Samples = Converted.data();
			const double Trim = SeekTarget.load() - Pts;
			if (Trim > 0.0)
			{
				const int Skip = std::min(Count, int(Trim * OutSampleRate));
				Samples += size_t(Skip) * OutChannels;
				Count -= Skip;
				Pts += Skip / double(OutSampleRate);
				if (Count <= 0) continue;
			}

			// Wait for room: at most AudioAheadFrames queued, unless the mod stopped reading (then audio is dropped).
			while (!Stopping && Generation == SeekGeneration.load())
			{
				if (Paused.load())
				{
					Sleep(10);
					continue;
				}
				if (WallClock)
				{
					break;
				}
				if (Holding.load())
				{
					AudioReadyGeneration = Generation;
					Sleep(2);
					continue;
				}
				if (AudioWriter->QueuedFrames() + Count <= AudioAheadFrames) break;
				Sleep(5);
			}
			if (Stopping || Generation != SeekGeneration.load() || WallClock) continue;
			{
				std::lock_guard<std::mutex> Lock(AudioMapMutex);
				AudioMap.push_back({AudioWriter->GetWriteFrames(), Pts});
				while (AudioMap.size() > 256) AudioMap.pop_front();
			}
			AudioWriter->Write(Samples, uint32_t(Count));
		}
	}
	av_frame_free(&Frame);
}

double FMediaSession::PtsForRingFrame(int64_t Frame)
{
	std::lock_guard<std::mutex> Lock(AudioMapMutex);
	if (AudioMap.empty() || Frame < AudioMap.front().first) return NAN;
	auto It = std::upper_bound(AudioMap.begin(), AudioMap.end(), Frame, [](int64_t F, const auto& Entry) { return F < Entry.first; });
	--It;
	return It->second + (Frame - It->first) / double(OutSampleRate);
}

double FMediaSession::ComputeClock(double Now)
{
	if (Paused || Holding) return HoldPosition;
	if (!WallClock && AudioWriter)
	{
		const int64_t Read = AudioWriter->GetReadFrames();
		if (Read != LastReadFrames)
		{
			LastReadFrames = Read;
			LastReadChange = Now;
		}
		if (Now - LastReadChange > ConsumerIdleSeconds && AudioWriter->QueuedFrames() > 0)
		{
			LOG_WARN("session %u: audio not being read; following the wall clock", Params.SessionId);
			WallClock = true;
			WallBase = LastClock;
			WallStart = Now;
			WallRate = 1.0;
		}
		else
		{
			const double ReadPts = PtsForRingFrame(Read);
			if (!std::isnan(ReadPts)) return ReadPts + std::min(Now - LastReadChange, 0.06);
			return LastClock;
		}
	}
	return WallBase + (Now - WallStart) * WallRate;
}

void FMediaSession::ControlSync(double Now, double Clock)
{
	double Target;
	bool WantPaused;
	{
		std::lock_guard<std::mutex> Lock(TargetMutex);
		if (!TargetValid) return;
		WantPaused = TargetPaused;
		Target = TargetMedia + (TargetPaused ? 0.0 : Now - TargetReceivedAt);
	}

	if (WantPaused != Paused.load())
	{
		if (WantPaused)
		{
			HoldPosition = Clock;
			Paused = true;
			if (AudioWriter) AudioWriter->DiscardQueued();
			{
				std::lock_guard<std::mutex> Lock(AudioMapMutex);
				AudioMap.clear();
			}
			LOG_INFO("session %u: paused at %.2f", Params.SessionId, Clock);
			SendState(ESessionState::Paused, Clock);
		}
		else
		{
			Paused = false;
			LOG_INFO("session %u: resumed; target %.2f", Params.SessionId, Target);
			if (!IsLive) RequestSeek(Target);
		}
		return;
	}
	if (Paused || Holding || IsLive) return;

	const double Drift = Clock - Target;
	if (std::abs(Drift) > SeekThreshold)
	{
		LOG_INFO("session %u: %.2f s off target (clock %.2f, target %.2f); seeking", Params.SessionId, Drift, Clock, Target);
		RequestSeek(Target + LastSeekLatency);
		return;
	}
	const double Correction = std::abs(Drift) > DriftTolerance ? std::clamp(Drift, -MaxCompensation, MaxCompensation) : 0.0;
	if (!WallClock)
	{
		// Each compensation spans one second of output, so it is re-armed every second while the drift lasts.
		const int32_t Delta = int32_t(Correction * OutSampleRate);
		if (Delta != CompensationDelta.load() || (Delta != 0 && Now >= NextCompensation))
		{
			CompensationDelta = Delta;
			CompensationPending = true;
			NextCompensation = Now + 1.0;
		}
	}
	else
	{
		WallBase = Clock;
		WallStart = Now;
		WallRate = 1.0 - Correction;
	}
}

bool FMediaSession::EnsureVideoWriter(uint32_t Width, uint32_t Height)
{
	if (VideoWriter && VideoWriter->GetWidth() == Width && VideoWriter->GetHeight() == Height) return true;
	auto Writer = std::make_unique<FVideoWriter>();
	const uint32_t Generation = ++VideoGeneration;
	const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-video" +
		std::to_string(Generation);
	if (!Writer->Create(Name, Width, Height))
	{
		return false;
	}
	VideoWriter = std::move(Writer);
	LOG_INFO("session %u: video %ux%u (generation %u)", Params.SessionId, Width, Height, Generation);
	if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, Width, Height, Name);
	return true;
}

void FMediaSession::PublishDueFrames(double Clock)
{
	std::unique_lock<std::mutex> Lock(FrameMutex);
	const uint32_t Generation = SeekGeneration.load();
	while (!Frames.empty() && Frames.front().Generation != Generation)
	{
		FramePool.push_back(std::move(Frames.front().Bgra));
		Frames.pop_front();
	}
	if (Frames.empty()) return;

	// While holding (after open or a seek) show the first frame at once; otherwise show the newest frame that is due.
	if (!Holding)
	{
		while (Frames.size() >= 2 && Frames[1].Pts <= Clock)
		{
			FramePool.push_back(std::move(Frames.front().Bgra));
			Frames.pop_front();
		}
		if (Frames.front().Pts > Clock + 0.005) return;
	}
	FVideoFrame Frame = std::move(Frames.front());
	Frames.pop_front();
	const uint32_t Width = OutWidth, Height = OutHeight;
	Lock.unlock();
	FrameCv.notify_all();

	if (Frame.Bgra.size() == size_t(Width) * Height * 4 && EnsureVideoWriter(Width, Height))
	{
		VideoWriter->Publish(Frame.Bgra.data(), Frame.Pts);
		++FramesPublished;
	}
	Lock.lock();
	FramePool.push_back(std::move(Frame.Bgra));
}

void FMediaSession::PresentLoop()
{
	SendState(ESessionState::Buffering, HoldPosition);
	while (!Stopping)
	{
		const double Now = NowSeconds();

		// Leave the hold once the first frame is up and (with audio) the first audio has been written.
		if (Holding && !Paused)
		{
			bool HaveVideo = !VideoCodec || FramesPublished.load() > 0;
			{
				std::lock_guard<std::mutex> Lock(FrameMutex);
				HaveVideo = HaveVideo || !Frames.empty();
			}
			const bool HaveAudio = WallClock || !AudioWriter || AudioReadyGeneration.load() == SeekGeneration.load();
			if (HaveVideo && HaveAudio)
			{
				Holding = false;
				WallBase = HoldPosition;
				WallStart = Now;
				LastReadChange = Now;
				LastSeekLatency = std::clamp(Now - SeekStartedAt, 0.1, 3.0);
				LOG_INFO("session %u: playing from %.2f (%.2f s to start)", Params.SessionId, HoldPosition, LastSeekLatency);
			}
		}

		const double Clock = ComputeClock(Now);
		LastClock = Clock;
		PublishDueFrames(Clock);

		if (Now >= NextSyncCheck)
		{
			NextSyncCheck = Now + 0.25;
			ControlSync(Now, Clock);
		}

		auto Drained = [](FPacketQueue& Queue)
		{
			std::lock_guard<std::mutex> Lock(Queue.Mutex);
			return Queue.Eof && Queue.Items.empty();
		};
		bool FramesEmpty;
		{
			std::lock_guard<std::mutex> Lock(FrameMutex);
			FramesEmpty = Frames.empty();
		}
		const bool VideoDone = !VideoCodec || (Drained(*VideoPackets) && FramesEmpty);
		const bool AudioDone = !AudioCodec || (Drained(*AudioPackets) && AudioWriter->QueuedFrames() <= 0);
		if (!Ended && !Holding && VideoDone && AudioDone)
		{
			Ended = true;
			LOG_INFO("session %u: ended at %.2f", Params.SessionId, Clock);
			SendState(ESessionState::Ended, Clock);
		}
		if (Now >= NextStateReport && !Ended)
		{
			NextStateReport = Now + 0.5;
			SendState(Paused ? ESessionState::Paused : Holding ? ESessionState::Buffering : ESessionState::Playing, Clock, Title);
		}
		Sleep(2);
	}
}
