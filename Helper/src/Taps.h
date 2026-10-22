#pragma once

#include "Common.h"

#include <functional>

// Lets other sessions receive a session's published frames (BGRA, Width x Height).
namespace FrameTaps
{
	using FTap = std::function<void(const uint8_t* Bgra, int Width, int Height)>;

	// Adds a tap; the source's refresh function (if set) runs right after, so the tap gets the current frame.
	int Add(uint32_t SourceSession, FTap Tap);
	void Remove(int TapId);
	void Publish(uint32_t SourceSession, const uint8_t* Bgra, int Width, int Height);
	// Whether anything listens to SourceSession's frames.
	bool Has(uint32_t SourceSession);

	// Set by a source that keeps its current frame: republishes it. Cleared before the source is destroyed.
	void SetRefresh(uint32_t SourceSession, std::function<void()> Refresh);
	void ClearRefresh(uint32_t SourceSession);
}

// Lets other sessions receive a session's audio (interleaved float stereo, 48 kHz).
namespace AudioTaps
{
	using FTap = std::function<void(const float* Interleaved, int Frames)>;

	int Add(uint32_t SourceSession, FTap Tap);
	void Remove(int TapId);
	void Publish(uint32_t SourceSession, const float* Interleaved, int Frames);
}
