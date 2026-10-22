// SFModWwiseAudioInput.h (shared by BoomBoxPlus and Telescreen; keep identical in both)
#pragma once
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
class AkAudioBuffer;
struct AkAudioFormat;

// A mod that feeds Wwise Audio Input sounds. Every mod registers one with IModularFeatures under GetName(); the Audio
// Input callbacks (whichever mod installed them last) ask each registered source in turn.
class ISFModWwiseAudioInputSource : public IModularFeature
{
public:
	static FName GetName() { return FName(TEXT("SFModWwiseAudioInputSource")); }
	// Fills Buffer for PlayingId and returns true if this source plays that id; otherwise returns false untouched.
	virtual bool FillAudioInput(uint32 PlayingId, AkAudioBuffer* Buffer) = 0;
	// Sets Format for PlayingId and returns true if this source plays that id.
	virtual bool GetAudioInputFormat(uint32 PlayingId, AkAudioFormat& Format) = 0;
};
