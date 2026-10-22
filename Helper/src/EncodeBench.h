#pragma once

#include "Common.h"
#include "StreamCodec.h"

struct FEncodeBenchArgs
{
	std::string Input;
	FEncoderSettings Settings;
	int Frames = 600;
	std::wstring Out;
};

// Encodes and decodes Frames frames of Input with the stream codec and logs speed, bitrate and quality.
int RunEncodeBench(const FEncodeBenchArgs& Args);
