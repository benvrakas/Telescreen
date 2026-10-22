#include "Screen/TSSharedBuffers.h"
#include "Host/TSHostProtocol.h"
#include "Telescreen.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using namespace TSHostProtocol;

// The helper only runs on Windows clients; other platforms (the Linux dedicated server) get stubs.
#if PLATFORM_WINDOWS

TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> FTSSharedMapping::Open(const FString& Name)
{
	HANDLE Handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, false, *Name);
	if (!Handle)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Could not open shared section %s (error %u)"), *Name, GetLastError());
		return nullptr;
	}
	void* View = MapViewOfFile(Handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!View)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Could not map shared section %s (error %u)"), *Name, GetLastError());
		CloseHandle(Handle);
		return nullptr;
	}
	TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> Mapping = MakeShareable(new FTSSharedMapping());
	Mapping->Handle = Handle;
	Mapping->Data = static_cast<uint8*>(View);
	return Mapping;
}

FTSSharedMapping::~FTSSharedMapping()
{
	if (Data) UnmapViewOfFile(Data);
	if (Handle) CloseHandle(Handle);
}

bool FTSVideoReader::Open(const FString& MappingName)
{
	Close();
	Mapping = FTSSharedMapping::Open(MappingName);
	if (!Mapping.IsValid())
	{
		return false;
	}
	const FVideoHeader* Header = reinterpret_cast<const FVideoHeader*>(Mapping->GetData());
	if (Header->Magic != VideoMagic || Header->Version != Version)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Shared video %s has an unexpected header (magic %08x, version %u)"), *MappingName, Header->Magic, Header->Version);
		Mapping.Reset();
		return false;
	}
	Width = Header->Width;
	Height = Header->Height;
	SlotBytes = Header->SlotBytes;
	Front = 2;
	LastSequence = 0;
	return true;
}

void FTSVideoReader::Close()
{
	Mapping.Reset();
	Width = Height = SlotBytes = 0;
}

bool FTSVideoReader::FetchNewFrame(TArray<uint8>& OutBgra, double& OutPts)
{
	if (!Mapping.IsValid())
	{
		return false;
	}
	FVideoHeader* Header = reinterpret_cast<FVideoHeader*>(Mapping->GetData());
	if ((FPlatformAtomics::AtomicRead(&Header->State) & TripleFresh) == 0)
	{
		return false;
	}
	Front = FPlatformAtomics::InterlockedExchange(&Header->State, Front) & TripleSlotMask;
	const uint64 Sequence = Header->SlotSequence[Front];
	if (Sequence == LastSequence)
	{
		return false;
	}
	LastSequence = Sequence;
	OutPts = Header->SlotPts[Front];
	OutBgra.SetNumUninitialized(SlotBytes, EAllowShrinking::No);
	FMemory::Memcpy(OutBgra.GetData(), Mapping->GetData() + VideoDataOffset + uint64(SlotBytes) * Front, SlotBytes);
	return true;
}

TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> FTSAudioReader::Open(const FString& MappingName)
{
	TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> Mapping = FTSSharedMapping::Open(MappingName);
	if (!Mapping.IsValid())
	{
		return nullptr;
	}
	const FAudioHeader* Header = reinterpret_cast<const FAudioHeader*>(Mapping->GetData());
	if (Header->Magic != AudioMagic || Header->Version != Version || Header->Channels == 0 || Header->CapacityFrames == 0)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Shared audio %s has an unexpected header"), *MappingName);
		return nullptr;
	}
	TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Reader = MakeShared<FTSAudioReader, ESPMode::ThreadSafe>();
	Reader->Mapping = Mapping;
	Reader->SampleRate = Header->SampleRate;
	Reader->Channels = Header->Channels;
	Reader->Capacity = Header->CapacityFrames;
	return Reader;
}

int32 FTSAudioReader::Read(float* Out, int32 Frames)
{
	FAudioHeader* Header = reinterpret_cast<FAudioHeader*>(Mapping->GetData());
	const float* Samples = reinterpret_cast<const float*>(Mapping->GetData() + AudioDataOffset);
	int64 ReadFrames = FPlatformAtomics::AtomicRead(&Header->ReadFrames);
	const int64 Discard = FPlatformAtomics::AtomicRead(&Header->DiscardUntil);
	ReadFrames = FMath::Max(ReadFrames, Discard);
	const int64 Written = FPlatformAtomics::AtomicRead(&Header->WriteFrames);
	const int32 Available = int32(FMath::Clamp<int64>(Written - ReadFrames, 0, Frames));
	for (int32 Done = 0; Done < Available;)
	{
		const uint32 Index = uint32((ReadFrames + Done) % Capacity);
		const int32 Chunk = FMath::Min<int32>(Available - Done, int32(Capacity - Index));
		FMemory::Memcpy(Out + int64(Done) * Channels, Samples + int64(Index) * Channels, int64(Chunk) * Channels * sizeof(float));
		Done += Chunk;
	}
	FPlatformAtomics::InterlockedExchange(&Header->ReadFrames, ReadFrames + Available);
	return Available;
}

#else

TSharedPtr<FTSSharedMapping, ESPMode::ThreadSafe> FTSSharedMapping::Open(const FString& Name) { return nullptr; }
FTSSharedMapping::~FTSSharedMapping() {}
bool FTSVideoReader::Open(const FString& MappingName) { return false; }
void FTSVideoReader::Close() { Mapping.Reset(); }
bool FTSVideoReader::FetchNewFrame(TArray<uint8>& OutBgra, double& OutPts) { return false; }
TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> FTSAudioReader::Open(const FString& MappingName) { return nullptr; }
int32 FTSAudioReader::Read(float* Out, int32 Frames) { return 0; }

#endif
