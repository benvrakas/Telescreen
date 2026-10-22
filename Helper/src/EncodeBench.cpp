// --selftest-encode: measures the stream encoder and decoder on frames from a video file (Docs/Debugging.md).

#include "EncodeBench.h"
#include "StreamCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace
{
	// Reads frames from a file, scaled to Width x Height BGRA, looping at the end.
	class FFrameSource
	{
	public:
		bool Open(const std::string& Path, int InWidth, int InHeight)
		{
			Width = InWidth;
			Height = InHeight;
			if (avformat_open_input(&Format, Path.c_str(), nullptr, nullptr) < 0) return false;
			avformat_find_stream_info(Format, nullptr);
			const AVCodec* Decoder = nullptr;
			Stream = av_find_best_stream(Format, AVMEDIA_TYPE_VIDEO, -1, -1, &Decoder, 0);
			if (Stream < 0 || !Decoder) return false;
			Codec = avcodec_alloc_context3(Decoder);
			avcodec_parameters_to_context(Codec, Format->streams[Stream]->codecpar);
			Codec->thread_count = 0;
			if (avcodec_open2(Codec, Decoder, nullptr) < 0) return false;
			Packet = av_packet_alloc();
			Frame = av_frame_alloc();
			return true;
		}

		~FFrameSource()
		{
			avcodec_free_context(&Codec);
			avformat_close_input(&Format);
			av_packet_free(&Packet);
			av_frame_free(&Frame);
			sws_freeContext(Sws);
		}

		bool Next(std::vector<uint8_t>& Out)
		{
			for (int Attempts = 0; Attempts < 10000; ++Attempts)
			{
				if (avcodec_receive_frame(Codec, Frame) == 0)
				{
					Sws = sws_getCachedContext(Sws, Frame->width, Frame->height, AVPixelFormat(Frame->format), Width, Height, AV_PIX_FMT_BGRA, SWS_BILINEAR,
						nullptr, nullptr, nullptr);
					Out.resize(size_t(Width) * Height * 4);
					uint8_t* Dst[1] = {Out.data()};
					const int Stride[1] = {Width * 4};
					sws_scale(Sws, Frame->data, Frame->linesize, 0, Frame->height, Dst, Stride);
					av_frame_unref(Frame);
					return true;
				}
				if (av_read_frame(Format, Packet) < 0)
				{
					av_seek_frame(Format, -1, 0, AVSEEK_FLAG_BACKWARD);
					avcodec_flush_buffers(Codec);
					continue;
				}
				if (Packet->stream_index == Stream) avcodec_send_packet(Codec, Packet);
				av_packet_unref(Packet);
			}
			return false;
		}

	private:
		AVFormatContext* Format = nullptr;
		AVCodecContext* Codec = nullptr;
		AVPacket* Packet = nullptr;
		AVFrame* Frame = nullptr;
		SwsContext* Sws = nullptr;
		int Stream = -1;
		int Width = 0, Height = 0;
	};

	double CpuSeconds()
	{
		FILETIME Create, Exit, Kernel, User;
		GetProcessTimes(GetCurrentProcess(), &Create, &Exit, &Kernel, &User);
		auto ToSeconds = [](const FILETIME& T) { return (uint64_t(T.dwHighDateTime) << 32 | T.dwLowDateTime) / 1e7; };
		return ToSeconds(Kernel) + ToSeconds(User);
	}

	// PSNR of the luma of two BGRA images.
	double LumaPsnr(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B)
	{
		if (A.size() != B.size() || A.empty()) return 0.0;
		double Sum = 0.0;
		const size_t Pixels = A.size() / 4;
		for (size_t I = 0; I < Pixels; I += 7)
		{
			const double Ya = 0.114 * A[I * 4] + 0.587 * A[I * 4 + 1] + 0.299 * A[I * 4 + 2];
			const double Yb = 0.114 * B[I * 4] + 0.587 * B[I * 4 + 1] + 0.299 * B[I * 4 + 2];
			Sum += (Ya - Yb) * (Ya - Yb);
		}
		const double Mse = Sum / double((Pixels + 6) / 7);
		return Mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / Mse);
	}

	double Percentile(std::vector<double> Values, double P)
	{
		if (Values.empty()) return 0.0;
		std::sort(Values.begin(), Values.end());
		return Values[std::min(Values.size() - 1, size_t(P * (Values.size() - 1)))];
	}

	void SaveBmp(const std::wstring& Path, const std::vector<uint8_t>& Bgra, int Width, int Height)
	{
		FILE* File = nullptr;
		if (Path.empty() || _wfopen_s(&File, Path.c_str(), L"wb") != 0 || !File) return;
		BITMAPFILEHEADER Fh{};
		BITMAPINFOHEADER Ih{};
		Ih.biSize = sizeof(Ih);
		Ih.biWidth = Width;
		Ih.biHeight = -Height;
		Ih.biPlanes = 1;
		Ih.biBitCount = 32;
		Fh.bfType = 0x4D42;
		Fh.bfOffBits = sizeof(Fh) + sizeof(Ih);
		Fh.bfSize = Fh.bfOffBits + DWORD(Bgra.size());
		fwrite(&Fh, sizeof(Fh), 1, File);
		fwrite(&Ih, sizeof(Ih), 1, File);
		fwrite(Bgra.data(), Bgra.size(), 1, File);
		fclose(File);
	}
}

int RunEncodeBench(const FEncodeBenchArgs& Args)
{
	FFrameSource Source;
	if (!Source.Open(Args.Input, Args.Settings.Width, Args.Settings.Height))
	{
		LOG_ERROR("bench: cannot read %s", Args.Input.c_str());
		return 1;
	}
	FStreamEncoder Encoder;
	std::string Error;
	if (!Encoder.Open(Args.Settings, Error))
	{
		LOG_ERROR("bench: %s", Error.c_str());
		return 1;
	}
	FStreamDecoder Decoder;
	if (!Decoder.Open(Encoder.GetCodecId(), Error))
	{
		LOG_ERROR("bench: %s", Error.c_str());
		return 1;
	}
	LOG_INFO("bench: %s -> %s, %d frames of %dx%d at %d fps, %d kbps", Encoder.GetEncoderName().c_str(), Decoder.GetDecoderName().c_str(), Args.Frames,
		Args.Settings.Width, Args.Settings.Height, Args.Settings.Fps, Args.Settings.BitrateKbps);

	std::vector<uint8_t> Input, Output;
	std::vector<double> EncodeMs, DecodeMs, Psnr;
	size_t Bytes = 0;
	int Decoded = 0, Keyframes = 0;
	const double CpuStart = CpuSeconds(), WallStart = NowSeconds();
	double SourceSeconds = 0.0;
	for (int I = 0; I < Args.Frames; ++I)
	{
		const double S0 = NowSeconds();
		if (!Source.Next(Input)) break;
		SourceSeconds += NowSeconds() - S0;
		const int64_t Pts = int64_t(I) * 1000 / Args.Settings.Fps;
		std::vector<FEncodedPacket> Packets;
		const double T0 = NowSeconds();
		Encoder.Encode(Input.data(), Pts, I % (Args.Settings.Fps * 2) == 0, Packets);
		const double T1 = NowSeconds();
		EncodeMs.push_back((T1 - T0) * 1000.0);
		for (const FEncodedPacket& Packet : Packets)
		{
			Bytes += Packet.Data.size();
			Keyframes += Packet.bKey ? 1 : 0;
			int Width = 0, Height = 0;
			const double D0 = NowSeconds();
			if (Decoder.Decode(Packet, Output, Width, Height))
			{
				DecodeMs.push_back((NowSeconds() - D0) * 1000.0);
				++Decoded;
				if (I % 10 == 0) Psnr.push_back(LumaPsnr(Input, Output));
			}
		}
	}
	const double Wall = NowSeconds() - WallStart - SourceSeconds;
	const double Cpu = CpuSeconds() - CpuStart;
	const double VideoSeconds = double(EncodeMs.size()) / Args.Settings.Fps;
	double PsnrAvg = 0.0;
	for (double P : Psnr) PsnrAvg += P;
	PsnrAvg = Psnr.empty() ? 0.0 : PsnrAvg / Psnr.size();
	LOG_INFO("bench result: %s | encode avg %.2f ms p95 %.2f ms | decode (%s) avg %.2f ms p95 %.2f ms | %d/%zu frames decoded, %d keyframes |"
		" %.0f kbps actual | luma PSNR %.1f dB | encode+decode %.1f ms/frame of %.1f available | CPU %.0f%% of one core (incl. source decode)",
		Encoder.GetEncoderName().c_str(), EncodeMs.empty() ? 0.0 : [&] { double S = 0; for (double V : EncodeMs) S += V; return S / EncodeMs.size(); }(),
		Percentile(EncodeMs, 0.95), Decoder.GetDecoderName().c_str(), DecodeMs.empty() ? 0.0 : [&] { double S = 0; for (double V : DecodeMs) S += V; return S / DecodeMs.size(); }(),
		Percentile(DecodeMs, 0.95), Decoded, EncodeMs.size(), Keyframes, Bytes * 8.0 / 1000.0 / std::max(VideoSeconds, 0.001), PsnrAvg,
		Wall * 1000.0 / std::max<size_t>(EncodeMs.size(), 1), 1000.0 / Args.Settings.Fps, Cpu / std::max(Wall + SourceSeconds, 0.001) * 100.0);
	SaveBmp(Args.Out, Output, Args.Settings.Width, Args.Settings.Height);
	return Decoded > 0 ? 0 : 1;
}
