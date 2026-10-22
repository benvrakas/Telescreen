#pragma once

#include "Common.h"

struct FResolvedMedia
{
	bool Ok = false;
	std::string Title;
	double Duration = 0.0;
	bool IsLive = false;
	// One URL (muxed) or two (video, then audio).
	std::vector<std::string> StreamUrls;
	// With streams: the video's language and yt-dlp's subtitle lists (JSON).
	std::string Language;
	std::string SubtitlesJson;
	std::string AutomaticJson;
	std::string Error;
};

// Sets the QuickJS executable yt-dlp uses to solve YouTube's challenges (without one YouTube throttles or hides formats).
void SetYtDlpJsRuntime(const std::wstring& QuickJsPath);

// Runs yt-dlp on Url. With WantStreams, also picks formats up to MaxHeight and returns their direct URLs.
FResolvedMedia ResolveWithYtDlp(const std::wstring& YtDlpPath, const std::string& Url, bool WantStreams, uint32_t MaxHeight);
