#include "LoopbackSession.h"

#include <algorithm>
#include <map>

using TSHostProtocol::ESessionState;

namespace
{
	std::mutex GTapMutex;
	std::map<int, std::pair<uint32_t, FrameTaps::FTap>> GTaps;
	int GNextTap = 1;
}

int FrameTaps::Add(uint32_t SourceSession, FTap Tap)
{
	std::lock_guard<std::mutex> Lock(GTapMutex);
	const int Id = GNextTap++;
	GTaps[Id] = {SourceSession, std::move(Tap)};
	return Id;
}

void FrameTaps::Remove(int TapId)
{
	std::lock_guard<std::mutex> Lock(GTapMutex);
	GTaps.erase(TapId);
}

void FrameTaps::Publish(uint32_t SourceSession, const uint8_t* Bgra, int Width, int Height)
{
	std::lock_guard<std::mutex> Lock(GTapMutex);
	for (auto& Pair : GTaps)
	{
		if (Pair.second.first == SourceSession) Pair.second.second(Bgra, Width, Height);
	}
}

FLoopbackSession::FLoopbackSession(const FLoopbackParams& InParams, FSessionEvents InEvents)
	: Params(InParams), Events(std::move(InEvents))
{
	TapId = FrameTaps::Add(Params.SourceSessionId, [this](const uint8_t* Bgra, int Width, int Height)
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		Latest.assign(Bgra, Bgra + size_t(Width) * Height * 4);
		LatestWidth = Width;
		LatestHeight = Height;
		bFresh = true;
	});
	LOG_INFO("session %u: loopback of session %u at %d fps, %d kbps", Params.SessionId, Params.SourceSessionId, Params.Fps, Params.BitrateKbps);
	Worker = std::thread([this] { Run(); });
}

FLoopbackSession::~FLoopbackSession()
{
	FrameTaps::Remove(TapId);
	Stopping = true;
	if (Worker.joinable()) Worker.join();
	LOG_INFO("session %u: loopback closed", Params.SessionId);
}

void FLoopbackSession::Run()
{
	FStreamEncoder Encoder;
	FStreamDecoder Decoder;
	std::unique_ptr<FVideoWriter> Writer;
	uint32_t Generation = 0;
	int EncWidth = 0, EncHeight = 0;
	std::vector<uint8_t> Frame, Decoded;
	double EncodeMs = 0.0, DecodeMs = 0.0, LastKey = 0.0, NextReport = NowSeconds() + 1.0;
	size_t Bytes = 0;
	int Frames = 0;
	const double Interval = 1.0 / std::max(1, Params.Fps);
	double NextFrame = NowSeconds();
	if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Buffering, 0.0, 0.0, "Waiting for the browser's frames");

	while (!Stopping)
	{
		const double Now = NowSeconds();
		if (Now < NextFrame)
		{
			Sleep(1);
			continue;
		}
		NextFrame = std::max(NextFrame + Interval, Now - Interval);
		int Width = 0, Height = 0;
		{
			std::lock_guard<std::mutex> Lock(FrameMutex);
			if (!bFresh) continue;
			Frame.swap(Latest);
			Width = LatestWidth;
			Height = LatestHeight;
			bFresh = false;
		}
		if (Width != EncWidth || Height != EncHeight)
		{
			FEncoderSettings Settings;
			Settings.Width = Width & ~1;
			Settings.Height = Height & ~1;
			Settings.Fps = Params.Fps;
			Settings.BitrateKbps = Params.BitrateKbps;
			Settings.Prefer = Params.Prefer;
			std::string Error;
			if (!Encoder.Open(Settings, Error) || !Decoder.Open(Encoder.GetCodecId(), Error))
			{
				LOG_ERROR("session %u: %s", Params.SessionId, Error.c_str());
				if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Error, 0.0, 0.0, Error);
				return;
			}
			EncWidth = Width;
			EncHeight = Height;
			LastKey = 0.0;
		}
		std::vector<FEncodedPacket> Packets;
		const bool bKey = Now - LastKey > 2.0;
		if (bKey) LastKey = Now;
		const double T0 = NowSeconds();
		Encoder.Encode(Frame.data(), int64_t(Now * 1000.0), bKey, Packets);
		EncodeMs += (NowSeconds() - T0) * 1000.0;
		for (const FEncodedPacket& Packet : Packets)
		{
			Bytes += Packet.Data.size();
			int OutWidth = 0, OutHeight = 0;
			const double D0 = NowSeconds();
			if (!Decoder.Decode(Packet, Decoded, OutWidth, OutHeight)) continue;
			DecodeMs += (NowSeconds() - D0) * 1000.0;
			++Frames;
			if (!Writer || Writer->GetWidth() != uint32_t(OutWidth) || Writer->GetHeight() != uint32_t(OutHeight))
			{
				auto NewWriter = std::make_unique<FVideoWriter>();
				const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-video" +
					std::to_string(++Generation);
				if (!NewWriter->Create(Name, OutWidth, OutHeight)) continue;
				Writer = std::move(NewWriter);
				if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, OutWidth, OutHeight, Name);
			}
			Writer->Publish(Decoded.data(), NowSeconds());
		}
		if (Now >= NextReport && Events.OnState)
		{
			NextReport = Now + 1.0;
			char Stats[256];
			snprintf(Stats, sizeof(Stats), "%s -> %s  %dx%d  %d fps  %.1f Mbps  encode %.1f ms  decode %.1f ms", Encoder.GetEncoderName().c_str(),
				Decoder.GetDecoderName().c_str(), EncWidth, EncHeight, Frames, Bytes * 8.0 / 1e6, Frames ? EncodeMs / Frames : 0.0,
				Frames ? DecodeMs / Frames : 0.0);
			Events.OnState(Params.SessionId, ESessionState::Playing, 0.0, 0.0, Stats);
			Bytes = 0;
			Frames = 0;
			EncodeMs = DecodeMs = 0.0;
		}
	}
}
