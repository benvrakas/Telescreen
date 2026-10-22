#include "SharedMemory.h"
#include "Security.h"

#include <algorithm>

using namespace TSHostProtocol;

bool FSharedSection::Create(const std::string& Name, size_t Size)
{
	Close();
	const std::wstring WideName = Widen(Name);
	FUserOnlySecurity Security;
	Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, Security.Get(), PAGE_READWRITE, DWORD(uint64_t(Size) >> 32), DWORD(Size & 0xFFFFFFFF),
		WideName.c_str());
	if (!Mapping)
	{
		LOG_ERROR("CreateFileMapping(%s, %zu) failed: %lu", Name.c_str(), Size, GetLastError());
		return false;
	}
	View = static_cast<uint8_t*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, Size));
	if (!View)
	{
		LOG_ERROR("MapViewOfFile(%s) failed: %lu", Name.c_str(), GetLastError());
		Close();
		return false;
	}
	memset(View, 0, Size);
	SectionName = Name;
	return true;
}

void FSharedSection::Close()
{
	if (View) UnmapViewOfFile(View);
	if (Mapping) CloseHandle(Mapping);
	View = nullptr;
	Mapping = nullptr;
}

bool FVideoWriter::Create(const std::string& Name, uint32_t InWidth, uint32_t InHeight)
{
	const uint32_t SlotBytes = InWidth * InHeight * 4;
	if (!Section.Create(Name, VideoDataOffset + size_t(SlotBytes) * 3))
	{
		return false;
	}
	Width = InWidth;
	Height = InHeight;
	Header = reinterpret_cast<FVideoHeader*>(Section.Data());
	Header->Version = Version;
	Header->Width = Width;
	Header->Height = Height;
	Header->Stride = Width * 4;
	Header->SlotBytes = SlotBytes;
	Header->State = 1;
	Back = 0;
	MemoryBarrier();
	Header->Magic = VideoMagic;
	return true;
}

void FVideoWriter::Publish(const uint8_t* Bgra, double Pts)
{
	if (!Header) return;
	uint8_t* Slot = Section.Data() + VideoDataOffset + size_t(Header->SlotBytes) * Back;
	memcpy(Slot, Bgra, Header->SlotBytes);
	Header->SlotPts[Back] = Pts;
	Header->SlotSequence[Back] = ++Sequence;
	const LONG Old = InterlockedExchange(reinterpret_cast<volatile LONG*>(&Header->State), Back | TripleFresh);
	Back = Old & TripleSlotMask;
}

bool FAudioWriter::Create(const std::string& Name, uint32_t InSampleRate, uint32_t InChannels, uint32_t CapacityFrames)
{
	if (!Section.Create(Name, AudioDataOffset + size_t(CapacityFrames) * InChannels * sizeof(float)))
	{
		return false;
	}
	SampleRate = InSampleRate;
	Channels = InChannels;
	Capacity = CapacityFrames;
	Header = reinterpret_cast<FAudioHeader*>(Section.Data());
	Samples = reinterpret_cast<float*>(Section.Data() + AudioDataOffset);
	Header->Version = Version;
	Header->SampleRate = SampleRate;
	Header->Channels = Channels;
	Header->CapacityFrames = Capacity;
	MemoryBarrier();
	Header->Magic = AudioMagic;
	return true;
}

int64_t FAudioWriter::GetWriteFrames() const
{
	return Header ? InterlockedCompareExchange64(&Header->WriteFrames, 0, 0) : 0;
}

int64_t FAudioWriter::GetReadFrames() const
{
	if (!Header) return 0;
	const int64_t Read = InterlockedCompareExchange64(&Header->ReadFrames, 0, 0);
	const int64_t Discard = InterlockedCompareExchange64(&Header->DiscardUntil, 0, 0);
	return std::max(Read, Discard);
}

int64_t FAudioWriter::QueuedFrames() const
{
	return GetWriteFrames() - GetReadFrames();
}

uint32_t FAudioWriter::FreeFrames() const
{
	const int64_t Queued = QueuedFrames();
	return Queued >= Capacity ? 0 : uint32_t(Capacity - Queued);
}

uint32_t FAudioWriter::Write(const float* Interleaved, uint32_t Frames)
{
	if (!Header) return 0;
	Frames = std::min(Frames, FreeFrames());
	const int64_t Start = GetWriteFrames();
	for (uint32_t Done = 0; Done < Frames;)
	{
		const uint32_t Index = uint32_t((Start + Done) % Capacity);
		const uint32_t Chunk = std::min(Frames - Done, Capacity - Index);
		memcpy(Samples + size_t(Index) * Channels, Interleaved + size_t(Done) * Channels, size_t(Chunk) * Channels * sizeof(float));
		Done += Chunk;
	}
	InterlockedExchange64(&Header->WriteFrames, Start + Frames);
	return Frames;
}

void FAudioWriter::DiscardQueued()
{
	if (Header) InterlockedExchange64(&Header->DiscardUntil, GetWriteFrames());
}
