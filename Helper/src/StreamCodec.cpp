#include "StreamCodec.h"

#include <algorithm>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace
{
	const char* Ladder[] = {"av1_nvenc", "av1_amf", "av1_qsv", "hevc_nvenc", "hevc_amf", "hevc_qsv", "h264_nvenc", "h264_amf", "h264_qsv"};

	std::string AvError(int Code)
	{
		char Buffer[256];
		av_strerror(Code, Buffer, sizeof(Buffer));
		return Buffer;
	}

	bool StartsWith(const std::string& Text, const char* Prefix)
	{
		return Text.rfind(Prefix, 0) == 0;
	}
}

FStreamEncoder::~FStreamEncoder()
{
	Close();
}

void FStreamEncoder::Close()
{
	avcodec_free_context(&Context);
	av_frame_free(&Frame);
	av_packet_free(&Packet);
	sws_freeContext(Sws);
	Sws = nullptr;
	EncoderName.clear();
}

bool FStreamEncoder::Open(const FEncoderSettings& Settings, std::string& Error)
{
	Close();
	std::vector<std::string> Order;
	const std::string& Prefer = Settings.Prefer;
	const bool bFamily = Prefer == "av1" || Prefer == "hevc" || Prefer == "h264" || Prefer == "software";
	if (Prefer == "software") Order.push_back("libsvtav1");
	else if (!Prefer.empty() && !bFamily) Order.push_back(Prefer);
	if (bFamily && Prefer != "software")
	{
		for (const char* Name : Ladder)
		{
			if (std::string(Name).rfind(Prefer + "_", 0) == 0) Order.push_back(Name);
		}
	}
	for (const char* Name : Ladder)
	{
		if (std::find(Order.begin(), Order.end(), Name) == Order.end()) Order.push_back(Name);
	}
	if ((Settings.bAllowSoftware || Prefer == "software") && std::find(Order.begin(), Order.end(), "libsvtav1") == Order.end()) Order.push_back("libsvtav1");
	std::string Tried;
	for (const std::string& Name : Order)
	{
		std::string Why;
		if (TryOpen(Name.c_str(), Settings, Why))
		{
			LOG_INFO("encoder: %s %dx%d @%d fps, %d kbps", Name.c_str(), Settings.Width, Settings.Height, Settings.Fps, Settings.BitrateKbps);
			return true;
		}
		LOG_INFO("encoder: skipping %s (%s)", Name.c_str(), Why.c_str());
		Tried += Name + " (" + Why + "); ";
		Close();
	}
	Error = "no usable video encoder: " + Tried;
	return false;
}

bool FStreamEncoder::TryOpen(const char* Name, const FEncoderSettings& Settings, std::string& Error)
{
	const AVCodec* Codec = avcodec_find_encoder_by_name(Name);
	if (!Codec)
	{
		Error = "not in this FFmpeg build";
		return false;
	}
	const std::string N = Name;
	const bool bNvenc = N.find("nvenc") != std::string::npos;
	const bool bAmf = N.find("_amf") != std::string::npos;
	const bool bQsv = N.find("_qsv") != std::string::npos;
	const bool bSvt = N == "libsvtav1";

	Context = avcodec_alloc_context3(Codec);
	Context->width = Settings.Width;
	Context->height = Settings.Height;
	// SVT-AV1 rejects a millisecond time base; it gets one tick per frame.
	FrameTimeBase = bSvt ? Settings.Fps : 0;
	Context->time_base = bSvt ? AVRational{1, Settings.Fps} : AVRational{1, 1000};
	Context->framerate = AVRational{Settings.Fps, 1};
	Context->bit_rate = int64_t(Settings.BitrateKbps) * 1000;
	Context->rc_max_rate = Context->bit_rate;
	Context->rc_buffer_size = int(Context->bit_rate / std::max(1, Settings.Fps) * 2);
	Context->gop_size = Settings.Fps * std::max(1, Settings.KeyframeSeconds);
	Context->max_b_frames = 0;
	Context->thread_count = bSvt ? 0 : 1;
	bBgraInput = bNvenc;
	Context->pix_fmt = bNvenc ? AV_PIX_FMT_BGRA : bSvt ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;

	AVDictionary* Options = nullptr;
	if (bNvenc)
	{
		av_dict_set(&Options, "preset", "p4", 0);
		av_dict_set(&Options, "tune", "ull", 0);
		av_dict_set(&Options, "rc", "cbr", 0);
		av_dict_set(&Options, "zerolatency", "1", 0);
		av_dict_set(&Options, "delay", "0", 0);
		av_dict_set(&Options, "forced-idr", "1", 0);
	}
	else if (bAmf)
	{
		av_dict_set(&Options, "usage", "ultralowlatency", 0);
		av_dict_set(&Options, "rc", "cbr", 0);
	}
	else if (bQsv)
	{
		av_dict_set(&Options, "preset", "veryfast", 0);
		av_dict_set(&Options, "async_depth", "1", 0);
		av_dict_set(&Options, "look_ahead", "0", 0);
	}
	else if (bSvt)
	{
		av_dict_set(&Options, "preset", "11", 0);
		av_dict_set(&Options, "svtav1-params", "pred-struct=1:lookahead=0:scd=0:fast-decode=1", 0);
	}
	const int Code = avcodec_open2(Context, Codec, &Options);
	av_dict_free(&Options);
	if (Code < 0)
	{
		Error = AvError(Code);
		return false;
	}
	Frame = av_frame_alloc();
	Frame->format = Context->pix_fmt;
	Frame->width = Settings.Width;
	Frame->height = Settings.Height;
	if (av_frame_get_buffer(Frame, 32) < 0)
	{
		Error = "frame allocation failed";
		return false;
	}
	Packet = av_packet_alloc();
	EncoderName = Name;
	CodecId = int(Codec->id);
	return true;
}

bool FStreamEncoder::Encode(const uint8_t* Bgra, int64_t PtsMs, bool bForceKeyframe, std::vector<FEncodedPacket>& Out)
{
	if (!Context) return false;
	if (av_frame_make_writable(Frame) < 0) return false;
	const int Width = Context->width, Height = Context->height;
	if (bBgraInput)
	{
		for (int Row = 0; Row < Height; ++Row) memcpy(Frame->data[0] + size_t(Row) * Frame->linesize[0], Bgra + size_t(Row) * Width * 4, size_t(Width) * 4);
	}
	else
	{
		Sws = sws_getCachedContext(Sws, Width, Height, AV_PIX_FMT_BGRA, Width, Height, AVPixelFormat(Context->pix_fmt), SWS_BILINEAR, nullptr, nullptr, nullptr);
		const uint8_t* Src[1] = {Bgra};
		const int SrcStride[1] = {Width * 4};
		sws_scale(Sws, Src, SrcStride, 0, Height, Frame->data, Frame->linesize);
	}
	Frame->pts = FrameTimeBase ? PtsMs * FrameTimeBase / 1000 : PtsMs;
	Frame->pict_type = bForceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
	if (bForceKeyframe) Frame->flags |= AV_FRAME_FLAG_KEY;
	else Frame->flags &= ~AV_FRAME_FLAG_KEY;
	const int Code = avcodec_send_frame(Context, Frame);
	if (Code < 0)
	{
		LOG_WARN("encoder: send_frame failed: %s", AvError(Code).c_str());
		return false;
	}
	Drain(Out);
	return true;
}

void FStreamEncoder::Drain(std::vector<FEncodedPacket>& Out)
{
	while (avcodec_receive_packet(Context, Packet) == 0)
	{
		FEncodedPacket Encoded;
		Encoded.Data.assign(Packet->data, Packet->data + Packet->size);
		Encoded.PtsMs = FrameTimeBase ? Packet->pts * 1000 / FrameTimeBase : Packet->pts;
		Encoded.bKey = (Packet->flags & AV_PKT_FLAG_KEY) != 0;
		Out.push_back(std::move(Encoded));
		av_packet_unref(Packet);
	}
}

FStreamDecoder::~FStreamDecoder()
{
	Close();
}

void FStreamDecoder::Close()
{
	avcodec_free_context(&Context);
	av_frame_free(&Frame);
	av_packet_free(&Pkt);
	sws_freeContext(Sws);
	Sws = nullptr;
}

bool FStreamDecoder::Open(int CodecId, std::string& Error)
{
	Close();
	const AVCodec* Codec = CodecId == AV_CODEC_ID_AV1 ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
	if (!Codec) Codec = avcodec_find_decoder(AVCodecID(CodecId));
	if (!Codec)
	{
		Error = "no decoder for codec " + std::to_string(CodecId);
		return false;
	}
	Context = avcodec_alloc_context3(Codec);
	Context->thread_count = 0;
	Context->flags |= AV_CODEC_FLAG_LOW_DELAY;
	const int Code = avcodec_open2(Context, Codec, nullptr);
	if (Code < 0)
	{
		Error = AvError(Code);
		return false;
	}
	Frame = av_frame_alloc();
	Pkt = av_packet_alloc();
	DecoderName = Codec->name;
	return true;
}

bool FStreamDecoder::Decode(const FEncodedPacket& Packet, std::vector<uint8_t>& OutBgra, int& OutWidth, int& OutHeight)
{
	if (!Context) return false;
	av_packet_unref(Pkt);
	if (av_new_packet(Pkt, int(Packet.Data.size())) < 0) return false;
	memcpy(Pkt->data, Packet.Data.data(), Packet.Data.size());
	Pkt->pts = Packet.PtsMs;
	if (Packet.bKey) Pkt->flags |= AV_PKT_FLAG_KEY;
	if (avcodec_send_packet(Context, Pkt) < 0) return false;
	bool bGot = false;
	while (avcodec_receive_frame(Context, Frame) == 0)
	{
		OutWidth = Frame->width;
		OutHeight = Frame->height;
		Sws = sws_getCachedContext(Sws, Frame->width, Frame->height, AVPixelFormat(Frame->format), Frame->width, Frame->height, AV_PIX_FMT_BGRA,
			SWS_BILINEAR, nullptr, nullptr, nullptr);
		OutBgra.resize(size_t(Frame->width) * Frame->height * 4);
		uint8_t* Dst[1] = {OutBgra.data()};
		const int DstStride[1] = {Frame->width * 4};
		sws_scale(Sws, Frame->data, Frame->linesize, 0, Frame->height, Dst, DstStride);
		av_frame_unref(Frame);
		bGot = true;
	}
	return bGot;
}
