#pragma once

#include "Common.h"

#include <string>
#include <vector>

struct FSubtitleCue
{
	uint32_t StartMs = 0;
	uint32_t EndMs = 0;
	std::string Text;
};

enum class ESubtitleKind : uint8_t
{
	// From the video's site: made by people, or generated (YouTube's automatic captions).
	Site = 0,
	Automatic = 1,
	// A text subtitle stream inside a video file.
	Embedded = 2,
	// A .srt or .vtt file next to a video file.
	File = 3,
	// A picture subtitle stream inside a video file (Blu-ray PGS, DVD): drawn into the picture by the helper.
	Picture = 4,
};

struct FSubtitleTrack
{
	uint32_t Id = 0;
	std::string Language;
	std::string Label;
	ESubtitleKind Kind = ESubtitleKind::Site;
	// Site and Automatic: where to download it, and its format ("json3", "vtt", "srt").
	std::string Url;
	std::string Format;
	// Embedded: the stream index in the file. File: its path.
	int Stream = -1;
	std::string Path;
};

namespace Subtitles
{
	// The tracks yt-dlp listed (its "subtitles" and "automatic_captions" as JSON). Automatic captions are kept for the
	// video's own language and Preferred only.
	void AddSiteTracks(const std::string& SubtitlesJson, const std::string& AutomaticJson, const std::string& VideoLanguage,
		const std::string& Preferred, std::vector<FSubtitleTrack>& Tracks);

	// .srt and .vtt files next to VideoPath named like it ("Movie.srt", "Movie.en.srt").
	void AddFileTracks(const std::string& VideoPath, std::vector<FSubtitleTrack>& Tracks);

	// Downloads (Site, Automatic) or reads (File) a track and parses it into cues sorted by start.
	bool Load(const FSubtitleTrack& Track, std::vector<FSubtitleCue>& Cues, std::string& Error);

	bool ParseJson3(const std::string& Data, std::vector<FSubtitleCue>& Cues);
	// WebVTT and SubRip.
	bool ParseText(const std::string& Data, std::vector<FSubtitleCue>& Cues);
	// The text of an ASS dialogue line from FFmpeg's subtitle decoders, without styling.
	std::string FromAss(const std::string& Dialogue);
	// Removes tags (<i>, {\an8}, <00:00:01.000>) and surrounding space; keeps line breaks.
	std::string Clean(const std::string& Text);
}
