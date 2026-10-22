#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace TSNetRate
{
	// Bytes per second a connection carrying a stream gets: room for the stream on top of the game's own traffic.
	constexpr int32 StreamRate = 1500000;

	// Raises the world's network send rate to at least BytesPerSecond: the driver caps, and every client connection on a
	// server or the server connection on a client. The game's defaults (60-300 KB/s) are shared with all gameplay
	// traffic and are too low for a video stream. Safe to call repeatedly; never lowers a rate.
	void Raise(UWorld* World, int32 BytesPerSecond);
}
