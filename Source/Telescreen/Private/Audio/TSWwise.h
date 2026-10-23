#pragma once

#include "CoreMinimal.h"

class AkAudioBuffer;
struct AkAudioFormat;

// The game's own Wwise sound engine, reached through its exported functions (the mod doesn't import them: a missing
// import stops the mod from loading), and helpers for banks the mod builds or loads from memory.
namespace TSWwise
{
	// AK::SoundEngine functions found by their exported names in the game's Wwise DLL.
	struct FCalls
	{
		int32 (*LoadBankMemoryCopy)(const void*, uint32, uint32&, uint32&) = nullptr;
		uint32 (*PostEvent)(uint32, uint64, uint32, void (*)(int32, void*, void*, void*), void*, uint32, void*, uint32) = nullptr;
		int32 (*RegisterGameObj)(uint64, const char*) = nullptr;
		void (*StopPlayingID)(uint32, int32, int32) = nullptr;
		// Only needed for RegisterBuiltInPlugin, so not part of IsComplete.
		int32 (*RegisterPlugin)(int32, uint32, uint32, void*, void*, void*) = nullptr;
		// A game parameter on one game object, and handing it back to the global value (not part of IsComplete).
		int32 (*SetRTPCValue)(uint32, float, uint64, int32, int32, bool) = nullptr;
		int32 (*ResetRTPCValue)(uint32, uint64, int32, int32, bool) = nullptr;

		bool IsComplete() const { return LoadBankMemoryCopy && PostEvent && RegisterGameObj && StopPlayingID; }
	};

	// The functions, looked up once; members are null where the game doesn't export them (or off Windows).
	const FCalls& GetCalls();

	// Wwise's id for a name: FNV-1 of the lowercase name (as AK::SoundEngine::GetIDFromString).
	uint32 IdFromName(const FString& Name);

	// Sets game parameter Id on one game object (overriding its global value there), or hands it back to the global
	// value. True if Wwise accepted it.
	bool SetObjectRtpc(uint32 Id, float Value, uint64 GameObject);
	bool ResetObjectRtpc(uint32 Id, uint64 GameObject);

	// One object of a bank's HIRC chunk: its type (2 sound, 3 action, 4 event, 7 actor-mixer, 17 plugin settings) and
	// byte range including its 5-byte header.
	struct FObject
	{
		uint8 Type = 0;
		int32 Start = 0;
		int32 Size = 0;

		uint32 GetId(const TArray<uint8>& Bytes) const;
	};

	// The HIRC objects of a bank file, in order, and where the HIRC chunk starts (or INDEX_NONE).
	TArray<FObject> ReadObjects(const TArray<uint8>& Bytes, int32& OutHircStart);

	// Prepares a bank built from one of the game's per-event banks for loading from memory: marks it a user bank in its
	// header and in its play actions, and puts sounds before the actor-mixers that list them. Logs each change under
	// Label.
	void PrepareBank(TArray<uint8>& Bytes, const TCHAR* Label);

	// Loads a bank from memory (Wwise keeps a copy). Logs the result under Label; returns true if it loaded.
	bool LoadBank(const TArray<uint8>& Bytes, const TCHAR* Label);

	// AkPluginID of Wwise's Audio Input source plugin.
	constexpr uint32 AudioInputPluginId = 0x00C80002;

	// Registers a plugin compiled into the game's Wwise DLL (AkPluginID: plugin id << 16 | company id << 4 | type)
	// that the game's Init bank doesn't list; Wwise only registers the ones it lists. Returns true if the plugin was
	// found and handed to Wwise (whatever Wwise answered: another mod may have registered it first).
	bool RegisterBuiltInPlugin(uint32 PluginId, const TCHAR* Label);

	// Audio Input callbacks (AK::AudioInputPlugin): fill a buffer for a playing id, and give its format.
	using FAudioInputExecute = void (*)(uint32 PlayingId, AkAudioBuffer* Buffer);
	using FAudioInputFormat = void (*)(uint32 PlayingId, AkAudioFormat& Format);

	// Hands the callbacks to the game's Audio Input plugin. There is one set for the whole game; returns false if the
	// game's sound engine couldn't be reached.
	bool SetAudioInputCallbacks(FAudioInputExecute Execute, FAudioInputFormat Format);
}
