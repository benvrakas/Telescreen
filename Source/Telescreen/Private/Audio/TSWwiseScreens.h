#pragma once

#include "CoreMinimal.h"

class FTSAudioReader;
class USceneComponent;

// Screen sound through the game's Wwise: each screen's sound plays as a Wwise Audio Input sound on the screen's own
// AkComponent, so the game's rooms, reverb, occlusion and mix apply to it. The sound comes from a bank built at runtime
// from one of the game's Boom Box song banks (the song's source swapped for Audio Input, every object given a TS_ id).
// The samples come from the helper's audio ring through the Audio Input callbacks, which are shared with other mods
// through ISFModWwiseAudioInputSource. See Docs/Architecture.md, Screen sound in Wwise.
namespace TSWwiseScreens
{
	// Builds and loads the bank, registers the Audio Input plugin and this mod's audio source, once per game. Returns
	// true if screens can play through Wwise.
	bool EnsureReady();

	// Removes this mod's audio source from the shared registry (module shutdown).
	void Shutdown();

	// Creates (once) the AkComponent screens play on, attached to Parent; nullptr if the AkAudio class isn't there.
	USceneComponent* CreateEmitter(USceneComponent* Parent);

	// Scales the emitter's attenuation distances (1 = the Boom Box falloff the bank came with).
	void SetAttenuationScale(USceneComponent* Emitter, float Scale);

	// Plays Reader's audio through Wwise on Emitter. Returns the playing id, or 0 if Wwise wouldn't play it.
	uint32 Play(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& Reader, USceneComponent* Emitter);

	void Stop(uint32 PlayingId);

	// Multiplier on the samples (screen volume, 0 mutes). The game's volume sliders apply in Wwise.
	void SetGain(uint32 PlayingId, float Gain);

	// True once Wwise ended this playing id itself (for example when the game stopped every sound on the emitter).
	bool HasEnded(uint32 PlayingId);

	// Moves Emitter's Wwise position, room and reverb volumes to where its component is now.
	void UpdatePosition(USceneComponent* Emitter);
}
