#pragma once

#include "CoreMinimal.h"

// A view of one of the helper's named shared memory sections.
class FTSSharedMapping
{
public:
	// Opens the section Name; null if it doesn't exist.
	static TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> Open(const FString& Name);
	~FTSSharedMapping();

	uint8* GetData() const { return Data; }

private:
	void* Handle = nullptr;
	uint8* Data = nullptr;
};

// Reader side of a session's video triple buffer (see Docs/HostProtocol.md).
class FTSVideoReader
{
public:
	bool Open(const FString& MappingName);
	void Close();
	bool IsOpen() const { return Mapping.IsValid(); }

	// Takes the newest frame if one arrived since the last call; copies it into OutBgra.
	bool FetchNewFrame(TArray<uint8>& OutBgra, double& OutPts);

	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }

private:
	TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> Mapping;
	uint32 Width = 0, Height = 0, SlotBytes = 0;
	int32 Front = 2;
	uint64 LastSequence = 0;
};

// Reader side of a session's audio ring; Read is called from the audio render thread.
class FTSAudioReader
{
public:
	static TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Open(const FString& MappingName);

	// Copies up to Frames interleaved float frames into Out; returns how many were available.
	int32 Read(float* Out, int32 Frames);

	uint32 GetSampleRate() const { return SampleRate; }
	uint32 GetChannels() const { return Channels; }

private:
	TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> Mapping;
	uint32 SampleRate = 0, Channels = 0, Capacity = 0;
};
