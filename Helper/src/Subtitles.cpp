#include "Subtitles.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

extern "C"
{
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

namespace
{
	// Largest subtitle file read (a feature film's subtitles are well under 1 MB).
	constexpr size_t MaxSubtitleBytes = 16u << 20;

	std::string Lower(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return char(std::tolower(C)); });
		return Text;
	}

	// The best format a track offers: json3 (YouTube), then vtt, then srt.
	bool PickFormat(const FJson& Formats, std::string& Url, std::string& Format)
	{
		for (const char* Want : {"json3", "vtt", "srt"})
		{
			for (const FJson& Entry : Formats.Array)
			{
				if (Entry["ext"].Str() == Want && !Entry["url"].Str().empty())
				{
					Url = Entry["url"].Str();
					Format = Want;
					return true;
				}
			}
		}
		return false;
	}

	std::string TrackName(const FJson& Formats, const std::string& Language)
	{
		for (const FJson& Entry : Formats.Array)
		{
			if (!Entry["name"].Str().empty()) return Entry["name"].Str();
		}
		return Language;
	}

	// "00:01:02.345", "01:02.345" or "00:01:02,345" -> milliseconds; false if Text isn't a timestamp.
	bool ParseTime(const std::string& Text, uint32_t& OutMs)
	{
		int Parts[3] = {0, 0, 0};
		int Count = 0, Ms = 0;
		size_t I = 0;
		while (I < Text.size() && Count < 3)
		{
			if (!std::isdigit(static_cast<unsigned char>(Text[I]))) return false;
			int Value = 0;
			while (I < Text.size() && std::isdigit(static_cast<unsigned char>(Text[I]))) Value = Value * 10 + (Text[I++] - '0');
			Parts[Count++] = Value;
			if (I < Text.size() && Text[I] == ':') { ++I; continue; }
			break;
		}
		if (I < Text.size() && (Text[I] == '.' || Text[I] == ','))
		{
			++I;
			int Digits = 0;
			while (I < Text.size() && std::isdigit(static_cast<unsigned char>(Text[I])) && Digits < 3)
			{
				Ms = Ms * 10 + (Text[I++] - '0');
				++Digits;
			}
			while (Digits++ < 3) Ms *= 10;
		}
		if (Count < 2) return false;
		const int Hours = Count == 3 ? Parts[0] : 0, Minutes = Count == 3 ? Parts[1] : Parts[0], Seconds = Count == 3 ? Parts[2] : Parts[1];
		OutMs = uint32_t(((Hours * 60 + Minutes) * 60 + Seconds) * 1000 + Ms);
		return true;
	}

	bool ReadUrl(const std::string& Url, std::string& Out, std::string& Error)
	{
		AVIOContext* Io = nullptr;
		AVDictionary* Options = nullptr;
		av_dict_set(&Options, "rw_timeout", "15000000", 0);
		const int Code = avio_open2(&Io, Url.c_str(), AVIO_FLAG_READ, nullptr, &Options);
		av_dict_free(&Options);
		if (Code < 0)
		{
			char Buffer[256];
			av_strerror(Code, Buffer, sizeof(Buffer));
			Error = Buffer;
			return false;
		}
		unsigned char Chunk[65536];
		int Read = 0;
		while ((Read = avio_read(Io, Chunk, sizeof(Chunk))) > 0 && Out.size() < MaxSubtitleBytes)
		{
			Out.append(reinterpret_cast<const char*>(Chunk), size_t(Read));
		}
		avio_closep(&Io);
		return true;
	}

	bool ReadFile(const std::string& Path, std::string& Out, std::string& Error)
	{
		std::ifstream File(Widen(Path), std::ios::binary);
		if (!File)
		{
			Error = "can't read " + Path;
			return false;
		}
		std::ostringstream Data;
		Data << File.rdbuf();
		Out = Data.str().substr(0, MaxSubtitleBytes);
		return true;
	}
}

void Subtitles::AddSiteTracks(const std::string& SubtitlesJson, const std::string& AutomaticJson, const std::string& VideoLanguage,
	const std::string& Preferred, std::vector<FSubtitleTrack>& Tracks)
{
	const std::string Video = Lower(VideoLanguage), Want = Lower(Preferred);
	auto Add = [&](const FJson& Map, ESubtitleKind Kind)
	{
		for (const auto& Pair : Map.Object)
		{
			const std::string Language = Pair.first;
			const std::string Base = Lower(Language.substr(0, Language.find('-')));
			if (Language == "live_chat") continue;
			// Automatic captions list every translation: keep the original language and the preferred one.
			if (Kind == ESubtitleKind::Automatic)
			{
				const bool bOriginal = Lower(Language) == Video + "-orig" || (!Video.empty() && Lower(Language) == Video);
				if (!bOriginal && Lower(Language) != Want && Base != Want) continue;
			}
			FSubtitleTrack Track;
			if (!PickFormat(Pair.second, Track.Url, Track.Format)) continue;
			Track.Id = uint32_t(Tracks.size() + 1);
			Track.Language = Language;
			Track.Label = TrackName(Pair.second, Language);
			Track.Kind = Kind;
			Tracks.push_back(std::move(Track));
		}
	};
	const FJson Site = FJson::Parse(SubtitlesJson);
	if (Site.IsObject()) Add(Site, ESubtitleKind::Site);
	const FJson Automatic = FJson::Parse(AutomaticJson);
	if (Automatic.IsObject()) Add(Automatic, ESubtitleKind::Automatic);
}

void Subtitles::AddFileTracks(const std::string& VideoPath, std::vector<FSubtitleTrack>& Tracks)
{
	const size_t Slash = VideoPath.find_last_of("/\\");
	const size_t Dot = VideoPath.find_last_of('.');
	if (Dot == std::string::npos || (Slash != std::string::npos && Dot < Slash)) return;
	const std::string Directory = Slash == std::string::npos ? std::string() : VideoPath.substr(0, Slash + 1);
	const std::string Stem = VideoPath.substr(Slash == std::string::npos ? 0 : Slash + 1, Dot - (Slash == std::string::npos ? 0 : Slash + 1));
	WIN32_FIND_DATAW Found;
	HANDLE Find = FindFirstFileW(Widen(Directory + Stem + "*").c_str(), &Found);
	if (Find == INVALID_HANDLE_VALUE) return;
	do
	{
		if (Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const std::string Name = Narrow(Found.cFileName);
		const std::string LowerName = Lower(Name);
		const bool bSrt = LowerName.size() > 4 && LowerName.compare(LowerName.size() - 4, 4, ".srt") == 0;
		const bool bVtt = LowerName.size() > 4 && LowerName.compare(LowerName.size() - 4, 4, ".vtt") == 0;
		if (!bSrt && !bVtt) continue;
		// "Movie.en.forced.srt" -> "en.forced".
		std::string Middle = Name.substr(Stem.size(), Name.size() - Stem.size() - 4);
		if (!Middle.empty() && Middle[0] == '.') Middle = Middle.substr(1);
		FSubtitleTrack Track;
		Track.Id = uint32_t(Tracks.size() + 1);
		Track.Language = Middle.substr(0, Middle.find('.'));
		Track.Label = Middle.empty() ? Name : Middle;
		Track.Kind = ESubtitleKind::File;
		Track.Path = Directory + Name;
		Track.Format = bSrt ? "srt" : "vtt";
		Tracks.push_back(std::move(Track));
	} while (FindNextFileW(Find, &Found));
	FindClose(Find);
}

bool Subtitles::Load(const FSubtitleTrack& Track, std::vector<FSubtitleCue>& Cues, std::string& Error)
{
	std::string Data;
	const bool bRead = Track.Kind == ESubtitleKind::File ? ReadFile(Track.Path, Data, Error) : ReadUrl(Track.Url, Data, Error);
	if (!bRead) return false;
	const bool bOk = Track.Format == "json3" ? ParseJson3(Data, Cues) : ParseText(Data, Cues);
	if (!bOk)
	{
		Error = "couldn't read the " + Track.Format + " subtitles";
		return false;
	}
	std::stable_sort(Cues.begin(), Cues.end(), [](const FSubtitleCue& A, const FSubtitleCue& B) { return A.StartMs < B.StartMs; });
	return true;
}

bool Subtitles::ParseJson3(const std::string& Data, std::vector<FSubtitleCue>& Cues)
{
	const FJson Root = FJson::Parse(Data);
	const FJson& Events = Root["events"];
	if (!Events.IsArray()) return false;
	for (const FJson& Event : Events.Array)
	{
		const FJson& Segs = Event["segs"];
		if (!Segs.IsArray()) continue;
		std::string Text;
		for (const FJson& Seg : Segs.Array) Text += Seg["utf8"].Str();
		Text = Clean(Text);
		if (Text.empty()) continue;
		FSubtitleCue Cue;
		Cue.StartMs = uint32_t(std::max(0.0, Event["tStartMs"].Num()));
		Cue.EndMs = Cue.StartMs + uint32_t(std::max(0.0, Event["dDurationMs"].Num(2000.0)));
		Cue.Text = std::move(Text);
		Cues.push_back(std::move(Cue));
	}
	return true;
}

bool Subtitles::ParseText(const std::string& Data, std::vector<FSubtitleCue>& Cues)
{
	std::istringstream Stream(Data);
	std::string Line;
	FSubtitleCue Cue;
	bool bInCue = false;
	auto Finish = [&]()
	{
		if (bInCue)
		{
			Cue.Text = Clean(Cue.Text);
			if (!Cue.Text.empty() && Cue.EndMs > Cue.StartMs) Cues.push_back(Cue);
		}
		bInCue = false;
		Cue = FSubtitleCue();
	};
	while (std::getline(Stream, Line))
	{
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();
		const size_t Arrow = Line.find("-->");
		if (Arrow != std::string::npos)
		{
			Finish();
			std::string From = Line.substr(0, Arrow), To = Line.substr(Arrow + 3);
			From.erase(0, From.find_first_not_of(" \t"));
			To.erase(0, To.find_first_not_of(" \t"));
			To = To.substr(0, To.find_first_of(" \t"));
			if (ParseTime(From, Cue.StartMs) && ParseTime(To, Cue.EndMs)) bInCue = true;
			continue;
		}
		if (Line.empty())
		{
			Finish();
			continue;
		}
		if (bInCue)
		{
			if (!Cue.Text.empty()) Cue.Text += '\n';
			Cue.Text += Line;
		}
	}
	Finish();
	return true;
}

std::string Subtitles::FromAss(const std::string& Dialogue)
{
	// "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text"; older FFmpeg lines start with "Dialogue: ".
	size_t At = 0;
	for (int Comma = 0; Comma < 8 && At != std::string::npos; ++Comma)
	{
		At = Dialogue.find(',', At);
		if (At != std::string::npos) ++At;
	}
	std::string Text = At == std::string::npos ? Dialogue : Dialogue.substr(At);
	std::string Out;
	for (size_t I = 0; I < Text.size(); ++I)
	{
		if (Text[I] == '\\' && I + 1 < Text.size() && (Text[I + 1] == 'N' || Text[I + 1] == 'n'))
		{
			Out += '\n';
			++I;
		}
		else if (Text[I] == '\\' && I + 1 < Text.size() && Text[I + 1] == 'h')
		{
			Out += ' ';
			++I;
		}
		else
		{
			Out += Text[I];
		}
	}
	return Clean(Out);
}

std::string Subtitles::Clean(const std::string& Text)
{
	std::string Out;
	int Angle = 0, Brace = 0;
	for (char C : Text)
	{
		if (C == '<') { ++Angle; continue; }
		if (C == '>' && Angle > 0) { --Angle; continue; }
		if (C == '{') { ++Brace; continue; }
		if (C == '}' && Brace > 0) { --Brace; continue; }
		if (Angle == 0 && Brace == 0) Out += C;
	}
	// Entities WebVTT uses.
	for (const auto& [From, To] : {std::pair<const char*, const char*>{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}, {"&quot;", "\""}, {"&#39;", "'"}})
	{
		for (size_t At = Out.find(From); At != std::string::npos; At = Out.find(From, At + 1)) Out.replace(At, std::strlen(From), To);
	}
	// Trim each line.
	std::string Result, Line;
	std::istringstream Lines(Out);
	while (std::getline(Lines, Line))
	{
		Line.erase(0, Line.find_first_not_of(" \t"));
		Line.erase(Line.find_last_not_of(" \t\r") + 1);
		if (Line.empty()) continue;
		if (!Result.empty()) Result += '\n';
		Result += Line;
	}
	return Result;
}
