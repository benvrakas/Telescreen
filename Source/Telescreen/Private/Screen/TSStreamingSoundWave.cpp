#include "Screen/TSStreamingSoundWave.h"
#include "Screen/TSSharedBuffers.h"

UTSStreamingSoundWave::UTSStreamingSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Keep pulling audio while inaudible, so the helper's clock keeps running.
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	bCanProcessAsync = true;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SetSampleRate(48000);
	NumChannels = 2;
}

void UTSStreamingSoundWave::SetReader(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& InReader)
{
	FScopeLock Lock(&ReaderLock);
	Reader = InReader;
	if (Reader.IsValid())
	{
		SetSampleRate(Reader->GetSampleRate());
		NumChannels = Reader->GetChannels();
	}
}

int32 UTSStreamingSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * sizeof(float), EAllowShrinking::No);
	float* Out = reinterpret_cast<float*>(OutAudio.GetData());
	TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Current;
	{
		FScopeLock Lock(&ReaderLock);
		Current = Reader;
	}
	int32 Filled = 0;
	if (Current.IsValid() && Current->GetChannels() > 0)
	{
		const int32 Channels = Current->GetChannels();
		Filled = Current->Read(Out, NumSamples / Channels) * Channels;
	}
	if (Filled < NumSamples)
	{
		FMemory::Memzero(Out + Filled, (NumSamples - Filled) * sizeof(float));
		if (Current.IsValid() && Filled > 0)
		{
			++Underruns;
		}
	}
	return NumSamples;
}

Audio::EAudioMixerStreamDataFormat::Type UTSStreamingSoundWave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Float;
}
