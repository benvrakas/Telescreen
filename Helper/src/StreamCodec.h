#pragma once

#include "Common.h"

#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

struct FEncodedPacket
{
	std::vector<uint8_t> Data;
	int64_t PtsMs = 0;
	bool bKey = false;
};

struct FEncoderSettings
{
	int Width = 1280;
	int Height = 720;
	int Fps = 30;
	int BitrateKbps = 3000;
	// Empty: best available. "av1", "hevc", "h264" or "software" (SVT-AV1): that family first. Otherwise an FFmpeg
	// encoder name to try first (e.g. "hevc_nvenc").
	std::string Prefer;
	bool bAllowSoftware = false;
	// Seconds between keyframes (keyframes can also be forced per frame).
	int KeyframeSeconds = 4;
};

// Low-latency video encoder over the plan's codec ladder: AV1, HEVC, H.264 on NVENC / AMF / QuickSync, then optional
// SVT-AV1 in software. Takes BGRA frames.
class FStreamEncoder
{
public:
	~FStreamEncoder();

	bool Open(const FEncoderSettings& Settings, std::string& Error);
	void Close();

	// Encodes one frame; packets that are ready are appended to Out.
	bool Encode(const uint8_t* Bgra, int64_t PtsMs, bool bForceKeyframe, std::vector<FEncodedPacket>& Out);

	const std::string& GetEncoderName() const { return EncoderName; }
	// AVCodecID of the stream (for the decoder).
	int GetCodecId() const { return CodecId; }

private:
	bool TryOpen(const char* Name, const FEncoderSettings& Settings, std::string& Error);
	void Drain(std::vector<FEncodedPacket>& Out);

	AVCodecContext* Context = nullptr;
	AVFrame* Frame = nullptr;
	AVPacket* Packet = nullptr;
	SwsContext* Sws = nullptr;
	std::string EncoderName;
	int CodecId = 0;
	bool bBgraInput = false;
	int FrameTimeBase = 0;
};

// Decoder for a stream made by FStreamEncoder; outputs BGRA.
class FStreamDecoder
{
public:
	~FStreamDecoder();

	bool Open(int CodecId, std::string& Error);
	void Close();

	// Decodes one packet; returns true and fills OutBgra when a frame comes out.
	bool Decode(const FEncodedPacket& Packet, std::vector<uint8_t>& OutBgra, int& OutWidth, int& OutHeight);

	const std::string& GetDecoderName() const { return DecoderName; }

private:
	AVCodecContext* Context = nullptr;
	AVFrame* Frame = nullptr;
	AVPacket* Pkt = nullptr;
	SwsContext* Sws = nullptr;
	std::string DecoderName;
};
