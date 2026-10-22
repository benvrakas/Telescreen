#pragma once

#include "Common.h"
#include "Host/TSHostProtocol.h"

// A named, pagefile-backed shared memory section.
class FSharedSection
{
public:
	FSharedSection() = default;
	~FSharedSection() { Close(); }
	FSharedSection(const FSharedSection&) = delete;
	FSharedSection& operator=(const FSharedSection&) = delete;

	// Creates a zeroed section of Size bytes named Name (e.g. Local\Telescreen-...).
	bool Create(const std::string& Name, size_t Size);
	void Close();

	uint8_t* Data() const { return View; }
	const std::string& GetName() const { return SectionName; }

private:
	HANDLE Mapping = nullptr;
	uint8_t* View = nullptr;
	std::string SectionName;
};

// Writer side of a session's video triple buffer (see Docs/HostProtocol.md).
class FVideoWriter
{
public:
	// Creates the section for Width x Height BGRA frames.
	bool Create(const std::string& Name, uint32_t Width, uint32_t Height);

	// Copies one BGRA frame (Width * 4 bytes per row, rows packed) and publishes it.
	void Publish(const uint8_t* Bgra, double Pts);

	uint32_t GetWidth() const { return Width; }
	uint32_t GetHeight() const { return Height; }
	const std::string& GetName() const { return Section.GetName(); }

private:
	FSharedSection Section;
	TSHostProtocol::FVideoHeader* Header = nullptr;
	uint32_t Width = 0, Height = 0;
	int32_t Back = 0;
	uint64_t Sequence = 0;
};

// Writer side of a session's audio ring (see Docs/HostProtocol.md).
class FAudioWriter
{
public:
	bool Create(const std::string& Name, uint32_t SampleRate, uint32_t Channels, uint32_t CapacityFrames);

	// Frames that fit right now.
	uint32_t FreeFrames() const;

	// Frames written but not yet read by the mod.
	int64_t QueuedFrames() const;

	// Appends up to Frames interleaved frames; returns how many were written.
	uint32_t Write(const float* Interleaved, uint32_t Frames);

	// Makes the reader skip everything written so far.
	void DiscardQueued();

	int64_t GetWriteFrames() const;
	int64_t GetReadFrames() const;
	uint32_t GetSampleRate() const { return SampleRate; }
	uint32_t GetChannels() const { return Channels; }
	uint32_t GetCapacity() const { return Capacity; }
	const std::string& GetName() const { return Section.GetName(); }

private:
	FSharedSection Section;
	TSHostProtocol::FAudioHeader* Header = nullptr;
	float* Samples = nullptr;
	uint32_t SampleRate = 0, Channels = 0, Capacity = 0;
};
