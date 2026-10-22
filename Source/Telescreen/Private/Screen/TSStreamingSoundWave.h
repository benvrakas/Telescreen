#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "TSStreamingSoundWave.generated.h"

class FTSAudioReader;

// Procedural sound wave that plays a media session's audio ring from the helper.
UCLASS()
class UTSStreamingSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UTSStreamingSoundWave(const FObjectInitializer& ObjectInitializer);

	// Starts playing from Reader; its sample rate and channel count must not change afterwards.
	void SetReader(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& Reader);

	// Number of audio callbacks that found the ring empty.
	int32 GetUnderruns() const { return Underruns.load(); }

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;

private:
	TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Reader;
	FCriticalSection ReaderLock;
	std::atomic<int32> Underruns{0};
};
