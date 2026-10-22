#include "MediaSession.h"
#include "YtDlp.h"
#include "Taps.h"
#include "Subtitles.h"

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
using TSHostProtocol::ECommand;

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
	FrameTaps::SetRefresh(Params.SessionId, [this]
	{
		std::lock_guard<std::mutex> Lock(TapMutex);
		if (!TapFrame.empty()) FrameTaps::Publish(Params.SessionId, TapFrame.data(), TapWidth, TapHeight);
	});
	SessionThread = std::thread([this] { Run(); });
}

FMediaSession::~FMediaSession()
{
	FrameTaps::ClearRefresh(Params.SessionId);
	Stopping = true;
	if (SubtitleLoader.joinable()) SubtitleLoader.join();
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
		FindSubtitleTracks(&Resolved);
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
	if (Params.IsLocalFile)
	{
		FindSubtitleTracks(nullptr);
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
	avcodec_free_context(&SubtitleCodec);
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
		std::lock_guard<std::mutex> TapLock(TapMutex);
		TapAudio.clear();
	}
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
		if (Packet->stream_index == SelectedSubtitleStream.load() && !Inputs.empty() && Input == Inputs.front().get())
		{
			DecodeSubtitlePacket(Input, Packet);
			av_packet_unref(Packet);
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
			{
				std::lock_guard<std::mutex> Lock(TapMutex);
				if (TapAudio.empty()) TapAudioStart = AudioWriter->GetWriteFrames();
				TapAudio.insert(TapAudio.end(), Samples, Samples + size_t(Count) * OutChannels);
				// At most 2 s waiting (the game stopped reading): drop the oldest.
				const size_t Max = size_t(OutSampleRate) * 2 * OutChannels;
				if (TapAudio.size() > Max)
				{
					const size_t Drop = TapAudio.size() - Max;
					TapAudio.erase(TapAudio.begin(), TapAudio.begin() + Drop);
					TapAudioStart += int64_t(Drop / OutChannels);
				}
			}
			AudioWriter->Write(Samples, uint32_t(Count));
		}
	}
	av_frame_free(&Frame);
}

void FMediaSession::FindSubtitleTracks(const FResolvedMedia* Resolved)
{
	std::vector<FSubtitleTrack> Tracks;
	if (Resolved)
	{
		Subtitles::AddSiteTracks(Resolved->SubtitlesJson, Resolved->AutomaticJson, Resolved->Language, Params.SubtitleLanguage, Tracks);
	}
	else if (!Inputs.empty())
	{
		AVFormatContext* Format = Inputs.front()->Format;
		for (unsigned I = 0; I < Format->nb_streams; ++I)
		{
			const AVStream* Stream = Format->streams[I];
			const AVCodecDescriptor* Descriptor = avcodec_descriptor_get(Stream->codecpar->codec_id);
			if (Stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE || !Descriptor || !(Descriptor->props & AV_CODEC_PROP_TEXT_SUB)) continue;
			const AVDictionaryEntry* Language = av_dict_get(Stream->metadata, "language", nullptr, 0);
			const AVDictionaryEntry* Name = av_dict_get(Stream->metadata, "title", nullptr, 0);
			FSubtitleTrack Track;
			Track.Id = uint32_t(Tracks.size() + 1);
			Track.Kind = ESubtitleKind::Embedded;
			Track.Stream = int(I);
			Track.Language = Language ? Language->value : "";
			Track.Label = Name ? Name->value : (Language ? Language->value : "Track " + std::to_string(Tracks.size() + 1));
			Tracks.push_back(std::move(Track));
		}
		Subtitles::AddFileTracks(Params.Url, Tracks);
	}
	for (size_t I = 0; I < Tracks.size(); ++I) Tracks[I].Id = uint32_t(I + 1);
	LOG_INFO("session %u: %zu subtitle track(s)", Params.SessionId, Tracks.size());
	{
		std::lock_guard<std::mutex> Lock(SubtitleMutex);
		SubtitleTracks = Tracks;
	}
	if (Events.OnSubtitleTracks) Events.OnSubtitleTracks(Params.SessionId, Tracks);
}

void FMediaSession::HandleCommand(ECommand Command, FPayloadReader& Reader)
{
	if (Command != ECommand::SelectSubtitles) return;
	const uint32_t Id = Reader.U32();
	if (!Reader.Ok) return;
	FSubtitleTrack Track;
	bool bFound = false;
	{
		std::lock_guard<std::mutex> Lock(SubtitleMutex);
		for (const FSubtitleTrack& Each : SubtitleTracks)
		{
			if (Each.Id == Id) { Track = Each; bFound = true; }
		}
	}
	const uint32_t Request = ++SubtitleRequest;
	SelectedSubtitleTrack = bFound ? Id : 0;
	SelectedSubtitleStream = bFound && Track.Kind == ESubtitleKind::Embedded ? Track.Stream : -1;
	if (Events.OnSubtitleCues) Events.OnSubtitleCues(Params.SessionId, bFound ? Id : 0, true, {});
	if (!bFound)
	{
		LOG_INFO("session %u: subtitles off", Params.SessionId);
		return;
	}
	if (SubtitleLoader.joinable()) SubtitleLoader.join();
	SubtitleLoader = std::thread([this, Track, Request]
	{
		std::vector<FSubtitleCue> Cues;
		std::string Error;
		const double Start = NowSeconds();
		// Embedded: the playing demuxer has read past what already played, so the whole track is read from the file
		// too; the demuxer's own lines cover the time until then.
		const bool bLoaded = Track.Kind == ESubtitleKind::Embedded ? ReadEmbeddedSubtitles(Track.Stream, Cues, Error) : Subtitles::Load(Track, Cues, Error);
		if (!bLoaded)
		{
			LOG_WARN("session %u: subtitles %s: %s", Params.SessionId, Track.Label.c_str(), Error.c_str());
			return;
		}
		LOG_INFO("session %u: subtitles %s (%s): %zu cues in %.1f s", Params.SessionId, Track.Label.c_str(), Track.Format.c_str(), Cues.size(), NowSeconds() - Start);
		if (Stopping || Request != SubtitleRequest.load()) return;
		if (Events.OnSubtitleCues) Events.OnSubtitleCues(Params.SessionId, Track.Id, true, Cues);
	});
}

bool FMediaSession::ReadEmbeddedSubtitles(int StreamIndex, std::vector<FSubtitleCue>& Cues, std::string& Error)
{
	AVFormatContext* Format = avformat_alloc_context();
	Format->interrupt_callback.callback = InterruptCallback;
	Format->interrupt_callback.opaque = &Stopping;
	if (avformat_open_input(&Format, Params.Url.c_str(), nullptr, nullptr) < 0)
	{
		Error = "couldn't open the file again";
		return false;
	}
	bool bOk = false;
	AVCodecContext* Codec = nullptr;
	if (StreamIndex >= 0 && unsigned(StreamIndex) < Format->nb_streams)
	{
		for (unsigned I = 0; I < Format->nb_streams; ++I) Format->streams[I]->discard = int(I) == StreamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
		AVStream* Stream = Format->streams[StreamIndex];
		const double Offset = Format->start_time != AV_NOPTS_VALUE ? Format->start_time / double(AV_TIME_BASE) : 0.0;
		if (const AVCodec* Decoder = avcodec_find_decoder(Stream->codecpar->codec_id))
		{
			Codec = avcodec_alloc_context3(Decoder);
			avcodec_parameters_to_context(Codec, Stream->codecpar);
			Codec->pkt_timebase = Stream->time_base;
			bOk = avcodec_open2(Codec, Decoder, nullptr) >= 0;
		}
		AVPacket* Packet = av_packet_alloc();
		while (bOk && !Stopping && av_read_frame(Format, Packet) >= 0)
		{
			if (Packet->stream_index == StreamIndex)
			{
				FSubtitleCue Cue;
				if (DecodeSubtitle(Codec, Stream, Offset, Packet, Cue)) Cues.push_back(std::move(Cue));
			}
			av_packet_unref(Packet);
		}
		av_packet_free(&Packet);
	}
	avcodec_free_context(&Codec);
	avformat_close_input(&Format);
	if (!bOk) Error = "no decoder for that subtitle stream";
	return bOk;
}

bool FMediaSession::DecodeSubtitle(AVCodecContext* Codec, AVStream* Stream, double StartOffset, AVPacket* Packet, FSubtitleCue& Cue)
{
	AVSubtitle Sub{};
	int Got = 0;
	if (avcodec_decode_subtitle2(Codec, &Sub, &Got, Packet) < 0 || !Got) return false;
	std::string Text;
	for (unsigned I = 0; I < Sub.num_rects; ++I)
	{
		const AVSubtitleRect* Rect = Sub.rects[I];
		const std::string Line = Rect->ass ? Subtitles::FromAss(Rect->ass) : Rect->text ? Subtitles::Clean(Rect->text) : std::string();
		if (Line.empty()) continue;
		if (!Text.empty()) Text += '\n';
		Text += Line;
	}
	avsubtitle_free(&Sub);
	const int64_t Pts = Packet->pts != AV_NOPTS_VALUE ? Packet->pts : Packet->dts;
	if (Text.empty() || Pts == AV_NOPTS_VALUE) return false;
	const double TimeBase = av_q2d(Stream->time_base);
	const double Start = Pts * TimeBase - StartOffset;
	const double Length = Packet->duration > 0 ? Packet->duration * TimeBase : 3.0;
	Cue.StartMs = uint32_t(std::max(0.0, Start) * 1000.0);
	Cue.EndMs = Cue.StartMs + uint32_t(std::clamp(Length, 0.3, 15.0) * 1000.0);
	Cue.Text = std::move(Text);
	return true;
}

void FMediaSession::DecodeSubtitlePacket(FInput* Input, AVPacket* Packet)
{
	AVStream* Stream = Input->Format->streams[Packet->stream_index];
	if (SubtitleCodecStream != Packet->stream_index)
	{
		avcodec_free_context(&SubtitleCodec);
		SubtitleCodecStream = Packet->stream_index;
		const AVCodec* Decoder = avcodec_find_decoder(Stream->codecpar->codec_id);
		if (Decoder)
		{
			SubtitleCodec = avcodec_alloc_context3(Decoder);
			avcodec_parameters_to_context(SubtitleCodec, Stream->codecpar);
			SubtitleCodec->pkt_timebase = Stream->time_base;
			if (avcodec_open2(SubtitleCodec, Decoder, nullptr) < 0) avcodec_free_context(&SubtitleCodec);
		}
		if (!SubtitleCodec) LOG_WARN("session %u: no decoder for subtitle stream %d", Params.SessionId, Packet->stream_index);
	}
	if (!SubtitleCodec) return;
	FSubtitleCue Cue;
	if (DecodeSubtitle(SubtitleCodec, Stream, Input->StartOffset, Packet, Cue) && Events.OnSubtitleCues)
	{
		Events.OnSubtitleCues(Params.SessionId, SelectedSubtitleTrack.load(), false, {Cue});
	}
}

void FMediaSession::PublishTapAudio()
{
	if (!AudioWriter) return;
	const int64_t Read = AudioWriter->GetReadFrames();
	std::lock_guard<std::mutex> Lock(TapMutex);
	if (TapAudio.empty()) return;
	if (Read < TapAudioStart)
	{
		// The ring was reset (a seek or pause): what's waiting will never play.
		if (TapAudioStart - Read > int64_t(OutSampleRate)) TapAudio.clear();
		return;
	}
	const int64_t Ready = std::min<int64_t>(Read - TapAudioStart, int64_t(TapAudio.size() / OutChannels));
	if (Ready <= 0) return;
	AudioTaps::Publish(Params.SessionId, TapAudio.data(), int(Ready));
	TapAudio.erase(TapAudio.begin(), TapAudio.begin() + size_t(Ready) * OutChannels);
	TapAudioStart += Ready;
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
				std::lock_guard<std::mutex> TapLock(TapMutex);
				TapAudio.clear();
			}
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
		if (FrameTaps::Has(Params.SessionId))
		{
			FrameTaps::Publish(Params.SessionId, Frame.Bgra.data(), int(Width), int(Height));
			std::lock_guard<std::mutex> TapLock(TapMutex);
			TapFrame = Frame.Bgra;
			TapWidth = int(Width);
			TapHeight = int(Height);
		}
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
		PublishTapAudio();

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
