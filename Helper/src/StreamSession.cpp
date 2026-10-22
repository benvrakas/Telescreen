#include "StreamSession.h"
#include "Taps.h"

#include <algorithm>
#include <cmath>

extern "C"
{
#include <libswscale/swscale.h>
}

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

using TSHostProtocol::ESessionState;

namespace
{
	constexpr int AudioRate = 48000;
	constexpr uint32_t RingFrames = AudioRate;
	// Receiver: audio queued beyond this is skipped so latency doesn't build up.
	constexpr int64_t MaxAudioQueued = AudioRate * 3 / 10;

	enum : uint8_t
	{
		ControlKeyframe = 0,
		ControlBitrate = 1,
		ControlReset = 2,
	};
}

FStreamSenderSession::FStreamSenderSession(const FSenderParams& InParams, FSessionEvents InEvents, FOnPacket InOnPacket)
	: Params(InParams), Events(std::move(InEvents)), OnPacket(std::move(InOnPacket))
{
	BitrateKbps = Params.BitrateKbps;
	StartedAt = NowSeconds();

	const AVCodec* Opus = avcodec_find_encoder_by_name("libopus");
	if (Opus)
	{
		AudioCodec = avcodec_alloc_context3(Opus);
		AudioCodec->sample_rate = AudioRate;
		AudioCodec->sample_fmt = AV_SAMPLE_FMT_FLT;
		av_channel_layout_default(&AudioCodec->ch_layout, 2);
		AudioCodec->bit_rate = int64_t(Params.AudioKbps) * 1000;
		AVDictionary* Options = nullptr;
		av_dict_set(&Options, "application", "lowdelay", 0);
		if (avcodec_open2(AudioCodec, Opus, &Options) < 0)
		{
			LOG_WARN("session %u: Opus encoder unavailable; the stream will have no sound", Params.SessionId);
			avcodec_free_context(&AudioCodec);
		}
		av_dict_free(&Options);
	}
	if (AudioCodec)
	{
		AudioFrame = av_frame_alloc();
		AudioFrame->format = AudioCodec->sample_fmt;
		AudioFrame->nb_samples = AudioCodec->frame_size;
		av_channel_layout_copy(&AudioFrame->ch_layout, &AudioCodec->ch_layout);
		av_frame_get_buffer(AudioFrame, 0);
		AudioPacket = av_packet_alloc();
	}

	VideoTap = FrameTaps::Add(Params.SourceSessionId, [this](const uint8_t* Bgra, int Width, int Height)
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		Latest.assign(Bgra, Bgra + size_t(Width) * Height * 4);
		LatestWidth = Width;
		LatestHeight = Height;
		bFresh = true;
	});
	AudioTap = AudioTaps::Add(Params.SourceSessionId, [this](const float* Interleaved, int Frames) { EncodeAudio(Interleaved, Frames); });
	LOG_INFO("session %u: streaming session %u at %d fps, %d kbps video, %d kbps audio", Params.SessionId, Params.SourceSessionId, Params.Fps,
		Params.BitrateKbps, Params.AudioKbps);
	VideoThread = std::thread([this] { RunVideo(); });
}

FStreamSenderSession::~FStreamSenderSession()
{
	FrameTaps::Remove(VideoTap);
	AudioTaps::Remove(AudioTap);
	Stopping = true;
	if (VideoThread.joinable()) VideoThread.join();
	std::lock_guard<std::mutex> Lock(AudioMutex);
	avcodec_free_context(&AudioCodec);
	av_frame_free(&AudioFrame);
	av_packet_free(&AudioPacket);
	sws_freeContext(Scaler);
	LOG_INFO("session %u: stream sender closed", Params.SessionId);
}

void FStreamSenderSession::HandleStreamControl(uint8_t Command, uint32_t Value)
{
	if (Command == ControlKeyframe)
	{
		KeyframeWanted = true;
	}
	else if (Command == ControlBitrate)
	{
		BitrateKbps = int(std::clamp<uint32_t>(Value, 200, 50000));
		LOG_INFO("session %u: bitrate set to %d kbps (applies on the next keyframe)", Params.SessionId, BitrateKbps.load());
		KeyframeWanted = true;
	}
}

void FStreamSenderSession::EncodeAudio(const float* Interleaved, int Frames)
{
	std::lock_guard<std::mutex> Lock(AudioMutex);
	if (!AudioCodec) return;
	AudioFifo.insert(AudioFifo.end(), Interleaved, Interleaved + size_t(Frames) * 2);
	const int FrameSize = AudioCodec->frame_size;
	while (AudioFifo.size() >= size_t(FrameSize) * 2)
	{
		if (av_frame_make_writable(AudioFrame) < 0) break;
		memcpy(AudioFrame->data[0], AudioFifo.data(), size_t(FrameSize) * 2 * sizeof(float));
		AudioFifo.erase(AudioFifo.begin(), AudioFifo.begin() + size_t(FrameSize) * 2);
		// The audio clock follows the wall clock: this chunk ends now, minus what is still buffered.
		const double EndMs = (NowSeconds() - StartedAt) * 1000.0 - (AudioFifo.size() / 2) * 1000.0 / AudioRate;
		AudioFrame->pts = AudioSamplesSent;
		AudioSamplesSent += FrameSize;
		if (avcodec_send_frame(AudioCodec, AudioFrame) < 0) continue;
		while (avcodec_receive_packet(AudioCodec, AudioPacket) == 0)
		{
			FStreamPacket Packet;
			Packet.Kind = EStreamKind::Audio;
			Packet.PtsMs = uint32_t(std::max(0.0, EndMs - FrameSize * 1000.0 / AudioRate));
			Packet.Codec = uint32_t(AV_CODEC_ID_OPUS);
			Packet.Data.assign(AudioPacket->data, AudioPacket->data + AudioPacket->size);
			av_packet_unref(AudioPacket);
			if (OnPacket) OnPacket(Params.SessionId, Packet);
		}
	}
}

void FStreamSenderSession::RunVideo()
{
	FStreamEncoder Encoder;
	int EncWidth = 0, EncHeight = 0, EncKbps = 0;
	std::vector<uint8_t> Frame;
	int FrameWidth = 0, FrameHeight = 0;
	const double Interval = 1.0 / std::max(1, Params.Fps);
	double NextFrame = NowSeconds(), NextReport = NowSeconds() + 1.0, LastKey = 0.0;
	uint64_t Bytes = 0;
	int Frames = 0;
	if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Buffering, 0.0, 0.0, "Starting the stream");
	while (!Stopping)
	{
		const double Now = NowSeconds();
		if (Now < NextFrame)
		{
			Sleep(1);
			continue;
		}
		NextFrame = std::max(NextFrame + Interval, Now - Interval);
		{
			std::lock_guard<std::mutex> Lock(FrameMutex);
			if (!bFresh && Frame.empty()) continue;
			if (bFresh)
			{
				Frame.swap(Latest);
				FrameWidth = LatestWidth;
				FrameHeight = LatestHeight;
				bFresh = false;
			}
		}
		int Width = FrameWidth, Height = FrameHeight;
		// Scaled to the stream resolution (the sharer's setting within the world host's limit).
		const uint8_t* Pixels = Frame.data();
		// Odd widths go through the scaler too, so the encoder's rows line up.
		const bool bTooTall = Params.MaxHeight > 0 && Height > Params.MaxHeight;
		if (bTooTall || (Width & 1))
		{
			const int OutHeight = std::max(2, (bTooTall ? Params.MaxHeight : Height) & ~1);
			const int OutWidth = std::max(2, int(int64_t(Width) * OutHeight / Height) & ~1);
			Scaler = sws_getCachedContext(Scaler, Width, Height, AV_PIX_FMT_BGRA, OutWidth, OutHeight, AV_PIX_FMT_BGRA, SWS_AREA, nullptr, nullptr, nullptr);
			if (Scaler)
			{
				Scaled.resize(size_t(OutWidth) * OutHeight * 4);
				const uint8_t* Src[1] = {Frame.data()};
				const int SrcStride[1] = {Width * 4};
				uint8_t* Dst[1] = {Scaled.data()};
				const int DstStride[1] = {OutWidth * 4};
				sws_scale(Scaler, Src, SrcStride, 0, Height, Dst, DstStride);
				Pixels = Scaled.data();
				Width = OutWidth;
				Height = OutHeight;
			}
		}
		Width &= ~1;
		Height &= ~1;
		if (Width != EncWidth || Height != EncHeight || EncKbps != BitrateKbps.load())
		{
			FEncoderSettings Settings;
			Settings.Width = Width;
			Settings.Height = Height;
			Settings.Fps = Params.Fps;
			Settings.BitrateKbps = BitrateKbps.load();
			Settings.Prefer = Params.Prefer;
			Settings.bAllowSoftware = Params.bAllowSoftware;
			std::string Error;
			if (!Encoder.Open(Settings, Error))
			{
				LOG_ERROR("session %u: %s", Params.SessionId, Error.c_str());
				if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Error, 0.0, 0.0, Error);
				return;
			}
			EncWidth = Width;
			EncHeight = Height;
			EncKbps = Settings.BitrateKbps;
			KeyframeWanted = true;
		}
		// Unchanged pages still send a frame a second, so late joiners and lost packets recover.
		const bool bKey = KeyframeWanted.exchange(false) || Now - LastKey > 4.0;
		if (bKey) LastKey = Now;
		std::vector<FEncodedPacket> Packets;
		Encoder.Encode(Pixels, int64_t((Now - StartedAt) * 1000.0), bKey, Packets);
		for (FEncodedPacket& Encoded : Packets)
		{
			FStreamPacket Packet;
			Packet.Kind = EStreamKind::Video;
			Packet.Flags = Encoded.bKey ? StreamFlagKey : 0;
			Packet.PtsMs = uint32_t(std::max<int64_t>(0, Encoded.PtsMs));
			Packet.Codec = uint32_t(Encoder.GetCodecId());
			Packet.Data = std::move(Encoded.Data);
			Bytes += Packet.Data.size();
			++Frames;
			if (OnPacket) OnPacket(Params.SessionId, Packet);
		}
		if (Now >= NextReport && Events.OnState)
		{
			NextReport = Now + 1.0;
			char Stats[200];
			snprintf(Stats, sizeof(Stats), "%s %dx%d %d fps %.1f Mbps", Encoder.GetEncoderName().c_str(), EncWidth, EncHeight, Frames, Bytes * 8.0 / 1e6);
			Events.OnState(Params.SessionId, ESessionState::Playing, 0.0, 0.0, Stats);
			Bytes = 0;
			Frames = 0;
		}
	}
}

FStreamReceiverSession::FStreamReceiverSession(uint32_t InSessionId, FSessionEvents InEvents)
	: SessionId(InSessionId), Events(std::move(InEvents))
{
	if (const AVCodec* Opus = avcodec_find_decoder(AV_CODEC_ID_OPUS))
	{
		AudioDecoder = avcodec_alloc_context3(Opus);
		AudioDecoder->sample_rate = AudioRate;
		av_channel_layout_default(&AudioDecoder->ch_layout, 2);
		if (avcodec_open2(AudioDecoder, Opus, nullptr) < 0) avcodec_free_context(&AudioDecoder);
	}
	AudioFrame = av_frame_alloc();
	AudioPkt = av_packet_alloc();
	if (Events.OnState) Events.OnState(SessionId, ESessionState::Buffering, 0.0, 0.0, "Waiting for the stream");
	VideoThread = std::thread([this] { RunVideo(); });
	PresenterThread = std::thread([this] { RunPresenter(); });
	LOG_INFO("session %u: stream receiver open", SessionId);
}

FStreamReceiverSession::~FStreamReceiverSession()
{
	Stopping = true;
	PacketCv.notify_all();
	if (VideoThread.joinable()) VideoThread.join();
	if (PresenterThread.joinable()) PresenterThread.join();
	std::lock_guard<std::mutex> Lock(AudioMutex);
	avcodec_free_context(&AudioDecoder);
	av_frame_free(&AudioFrame);
	av_packet_free(&AudioPkt);
	LOG_INFO("session %u: stream receiver closed", SessionId);
}

void FStreamReceiverSession::HandleStreamControl(uint8_t Command, uint32_t Value)
{
	if (Command == ControlReset)
	{
		WaitForKeyframe = true;
		std::lock_guard<std::mutex> Lock(PacketMutex);
		VideoPackets.clear();
	}
}

void FStreamReceiverSession::HandleStreamPacket(const FStreamPacket& Packet)
{
	BytesIn += Packet.Data.size();
	if (Packet.Kind == EStreamKind::Video)
	{
		if (WaitForKeyframe && !(Packet.Flags & StreamFlagKey)) return;
		WaitForKeyframe = false;
		std::lock_guard<std::mutex> Lock(PacketMutex);
		VideoPackets.push_back(Packet);
		while (VideoPackets.size() > 120) VideoPackets.pop_front();
		PacketCv.notify_all();
		return;
	}

	std::lock_guard<std::mutex> Lock(AudioMutex);
	if (!AudioDecoder) return;
	if (!AudioWriter)
	{
		AudioWriter = std::make_unique<FAudioWriter>();
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(SessionId) + "-audio";
		if (!AudioWriter->Create(Name, AudioRate, 2, RingFrames))
		{
			AudioWriter.reset();
			return;
		}
		if (Events.OnAudio) Events.OnAudio(SessionId, AudioRate, 2, RingFrames, Name);
	}
	av_packet_unref(AudioPkt);
	if (av_new_packet(AudioPkt, int(Packet.Data.size())) < 0) return;
	memcpy(AudioPkt->data, Packet.Data.data(), Packet.Data.size());
	if (avcodec_send_packet(AudioDecoder, AudioPkt) < 0) return;
	while (avcodec_receive_frame(AudioDecoder, AudioFrame) == 0)
	{
		const int Frames = AudioFrame->nb_samples;
		const int Channels = AudioFrame->ch_layout.nb_channels;
		Interleaved.resize(size_t(Frames) * 2);
		const bool bPlanar = av_sample_fmt_is_planar(AVSampleFormat(AudioFrame->format));
		for (int F = 0; F < Frames; ++F)
		{
			for (int C = 0; C < 2; ++C)
			{
				const int Src = std::min(C, Channels - 1);
				Interleaved[size_t(F) * 2 + C] = bPlanar ? reinterpret_cast<const float*>(AudioFrame->extended_data[Src])[F]
														 : reinterpret_cast<const float*>(AudioFrame->data[0])[size_t(F) * Channels + Src];
			}
		}
		av_frame_unref(AudioFrame);
		if (AudioWriter->QueuedFrames() > MaxAudioQueued)
		{
			AudioWriter->DiscardQueued();
			AudioMap.clear();
		}
		AudioMap.push_back({AudioWriter->GetWriteFrames(), Packet.PtsMs});
		while (AudioMap.size() > 256) AudioMap.pop_front();
		AudioWriter->Write(Interleaved.data(), uint32_t(Frames));
		LastAudioAt = NowSeconds();
	}
}

double FStreamReceiverSession::AudioClockMs()
{
	std::lock_guard<std::mutex> Lock(AudioMutex);
	if (!AudioWriter || AudioMap.empty() || NowSeconds() - LastAudioAt.load() > 0.5) return -1.0;
	const int64_t Read = AudioWriter->GetReadFrames();
	auto It = std::upper_bound(AudioMap.begin(), AudioMap.end(), Read, [](int64_t F, const auto& Entry) { return F < Entry.first; });
	if (It == AudioMap.begin()) return -1.0;
	--It;
	return It->second + (Read - It->first) * 1000.0 / AudioRate;
}

void FStreamReceiverSession::RunVideo()
{
	FStreamDecoder Decoder;
	uint32_t OpenCodec = 0;
	std::vector<uint8_t> Bgra;
	while (!Stopping)
	{
		FStreamPacket Packet;
		{
			std::unique_lock<std::mutex> Lock(PacketMutex);
			PacketCv.wait(Lock, [&] { return Stopping || !VideoPackets.empty(); });
			if (Stopping) break;
			Packet = std::move(VideoPackets.front());
			VideoPackets.pop_front();
		}
		if (Packet.Codec != OpenCodec)
		{
			std::string Error;
			if (!Decoder.Open(int(Packet.Codec), Error))
			{
				LOG_ERROR("session %u: %s", SessionId, Error.c_str());
				continue;
			}
			OpenCodec = Packet.Codec;
			CodecName = Decoder.GetDecoderName();
			LOG_INFO("session %u: decoding with %s", SessionId, CodecName.c_str());
		}
		FEncodedPacket Encoded;
		Encoded.Data = std::move(Packet.Data);
		Encoded.PtsMs = Packet.PtsMs;
		Encoded.bKey = (Packet.Flags & StreamFlagKey) != 0;
		int Width = 0, Height = 0;
		if (!Decoder.Decode(Encoded, Bgra, Width, Height)) continue;
		std::lock_guard<std::mutex> Lock(FrameMutex);
		Frames.push_back({Packet.PtsMs, Width, Height, Bgra});
		while (Frames.size() > 30) Frames.pop_front();
	}
}

void FStreamReceiverSession::RunPresenter()
{
	double NextReport = NowSeconds() + 1.0;
	bool bAnnounced = false;
	while (!Stopping)
	{
		const double Clock = AudioClockMs();
		FFrame Frame;
		bool bHave = false;
		{
			std::lock_guard<std::mutex> Lock(FrameMutex);
			// With sound, show the newest frame that is due on the audio clock; without, show frames as they arrive.
			while (!Frames.empty())
			{
				const bool bDue = Clock < 0.0 || Frames.front().PtsMs <= Clock + 10.0 || Frames.size() > 10;
				if (!bDue) break;
				Frame = std::move(Frames.front());
				Frames.pop_front();
				bHave = true;
			}
		}
		if (bHave)
		{
			if (!VideoWriter || VideoWriter->GetWidth() != uint32_t(Frame.Width) || VideoWriter->GetHeight() != uint32_t(Frame.Height))
			{
				auto Writer = std::make_unique<FVideoWriter>();
				const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(SessionId) + "-video" +
					std::to_string(++VideoGeneration);
				if (Writer->Create(Name, Frame.Width, Frame.Height))
				{
					VideoWriter = std::move(Writer);
					if (Events.OnVideo) Events.OnVideo(SessionId, VideoGeneration, Frame.Width, Frame.Height, Name);
				}
			}
			if (VideoWriter)
			{
				VideoWriter->Publish(Frame.Bgra.data(), Frame.PtsMs / 1000.0);
				++FramesShown;
				if (!bAnnounced && Events.OnState)
				{
					bAnnounced = true;
					Events.OnState(SessionId, ESessionState::Playing, 0.0, 0.0, "");
				}
			}
		}
		const double Now = NowSeconds();
		if (Now >= NextReport && Events.OnState && bAnnounced)
		{
			NextReport = Now + 1.0;
			char Stats[160];
			snprintf(Stats, sizeof(Stats), "%s %d fps %.1f Mbps", CodecName.c_str(), FramesShown.exchange(0), BytesIn.exchange(0) * 8.0 / 1e6);
			Events.OnState(SessionId, ESessionState::Playing, 0.0, 0.0, Stats);
		}
		Sleep(2);
	}
}
