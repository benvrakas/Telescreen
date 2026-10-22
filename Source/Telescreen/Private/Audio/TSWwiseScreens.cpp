#include "Audio/TSWwiseScreens.h"
#include "Audio/SFModWwiseAudioInput.h"
#include "Audio/TSWwise.h"
#include "Components/SceneComponent.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Screen/TSSharedBuffers.h"
#include "Telescreen.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/SoundEngine/Common/AkCallbackTypes.h>
#include <atomic>

namespace
{
	// Audio for one playing id, read on Wwise's audio thread.
	struct FScreenFeed
	{
		TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe> Reader;
		int32 SampleRate = 48000;
		int32 Channels = 2;
		std::atomic<float> Gain{0.f};
		std::atomic<bool> bEnded{false};
		// Wwise audio thread only.
		TArray<float> Scratch;
	};

	FCriticalSection ScreenFeedsLock;
	TMap<uint32, TSharedPtr<FScreenFeed>> ScreenFeeds;

	bool bScreensTried = false;
	bool bScreensReady = false;
	bool bScreenSourceRegistered = false;
	uint32 ScreenEventId = 0;

	// AK_EndOfEvent.
	constexpr uint32 ScreenEndOfEventFlag = 0x0001;

	// Body offsets of a Sound object: its source (plugin id, stream type, source id, cache id, media size, source
	// bits) takes bytes 4 to 21; the node settings follow.
	constexpr int32 ScreenSoundSourceEnd = 22;

	// Byte offsets in a bank file's BKHD: bank id (12), bank type (28).
	constexpr int32 ScreenBankIdOffset = 12;
	constexpr int32 ScreenBankTypeOffset = 28;

	const TCHAR* ScreenAkComponentClass = TEXT("/Script/AkAudio.AkComponent");

	// Telescreen's feeds, for the shared Audio Input callbacks.
	class FTelescreenScreenAudioSource : public ISFModWwiseAudioInputSource
	{
	public:
		virtual bool FillAudioInput(uint32 PlayingId, AkAudioBuffer* Buffer) override
		{
			TSharedPtr<FScreenFeed> Feed;
			{
				FScopeLock Lock(&ScreenFeedsLock);
				if (const TSharedPtr<FScreenFeed>* Found = ScreenFeeds.Find(PlayingId))
				{
					Feed = *Found;
				}
			}
			if (!Feed)
			{
				return false;
			}
			const uint32 Frames = Buffer->MaxFrames();
			const int32 Channels = FMath::Min<int32>(Buffer->NumChannels(), Feed->Channels);
			Feed->Scratch.SetNumUninitialized(Frames * Feed->Channels, EAllowShrinking::No);
			const int32 Got = Feed->Reader ? Feed->Reader->Read(Feed->Scratch.GetData(), Frames) : 0;
			const float Gain = Feed->Gain.load();
			for (int32 Channel = 0; Channel < Channels; ++Channel)
			{
				AkSampleType* Out = Buffer->GetChannel(Channel);
				for (int32 Frame = 0; Frame < Got; ++Frame)
				{
					Out[Frame] = Feed->Scratch[Frame * Feed->Channels + Channel] * Gain;
				}
				FMemory::Memzero(Out + Got, (Frames - Got) * sizeof(AkSampleType));
			}
			for (uint32 Channel = Channels; Channel < Buffer->NumChannels(); ++Channel)
			{
				FMemory::Memzero(Buffer->GetChannel(Channel), Frames * sizeof(AkSampleType));
			}
			Buffer->uValidFrames = static_cast<AkUInt16>(Frames);
			Buffer->eState = AK_DataReady;
			return true;
		}

		virtual bool GetAudioInputFormat(uint32 PlayingId, AkAudioFormat& Format) override
		{
			FScopeLock Lock(&ScreenFeedsLock);
			const TSharedPtr<FScreenFeed>* Found = ScreenFeeds.Find(PlayingId);
			if (!Found)
			{
				return false;
			}
			Format.uSampleRate = (*Found)->SampleRate;
			Format.channelConfig.SetStandard((*Found)->Channels == 1 ? AK_SPEAKER_SETUP_MONO : AK_SPEAKER_SETUP_STEREO);
			Format.uBlockAlign = sizeof(AkSampleType) * Format.channelConfig.uNumChannels;
			return true;
		}
	};

	FTelescreenScreenAudioSource ScreenAudioSource;

	// The shared Audio Input callbacks: every mod's source is asked in turn; whichever mod installed them last, all
	// mods' sounds play (SFModWwiseAudioInput.h).
	void DispatchScreenAudioInput(uint32 PlayingId, AkAudioBuffer* Buffer)
	{
		IModularFeatures::FScopedLockModularFeatureList Lock;
		for (ISFModWwiseAudioInputSource* Each :
			IModularFeatures::Get().GetModularFeatureImplementations<ISFModWwiseAudioInputSource>(ISFModWwiseAudioInputSource::GetName()))
		{
			if (Each && Each->FillAudioInput(PlayingId, Buffer))
			{
				return;
			}
		}
		// No source plays this id: silence (ZeroPadToMaxFrames only zeroes from uValidFrames on).
		Buffer->uValidFrames = 0;
		Buffer->ZeroPadToMaxFrames();
		Buffer->eState = AK_DataReady;
	}

	void DispatchScreenAudioFormat(uint32 PlayingId, AkAudioFormat& Format)
	{
		IModularFeatures::FScopedLockModularFeatureList Lock;
		for (ISFModWwiseAudioInputSource* Each :
			IModularFeatures::Get().GetModularFeatureImplementations<ISFModWwiseAudioInputSource>(ISFModWwiseAudioInputSource::GetName()))
		{
			if (Each && Each->GetAudioInputFormat(PlayingId, Format))
			{
				return;
			}
		}
		Format.uSampleRate = 48000;
		Format.channelConfig.SetStandard(AK_SPEAKER_SETUP_STEREO);
		Format.uBlockAlign = sizeof(AkSampleType) * Format.channelConfig.uNumChannels;
	}

	void HandleScreenEventCallback(int32 Type, void* EventInfo, void* CallbackInfo, void* Cookie)
	{
		if ((static_cast<uint32>(Type) & ScreenEndOfEventFlag) == 0 || !EventInfo)
		{
			return;
		}
		const uint32 PlayingId = static_cast<AkEventCallbackInfo*>(EventInfo)->playingID;
		FScopeLock Lock(&ScreenFeedsLock);
		if (const TSharedPtr<FScreenFeed>* Found = ScreenFeeds.Find(PlayingId))
		{
			(*Found)->bEnded = true;
		}
	}

	// Builds the screen bank from a game Boom Box song bank: the song's sound gets an Audio Input source (with its
	// plugin settings object before it), every object a new id, the bank a new id, and the actions point at it as a
	// user bank. Same layout as BoomBoxPlus's music bank, with TS_ ids.
	bool BuildScreenBank(const TArray<uint8>& Song, TArray<uint8>& OutBank, uint32& OutEventId)
	{
		int32 HircStart = INDEX_NONE;
		const TArray<TSWwise::FObject> Objects = TSWwise::ReadObjects(Song, HircStart);
		if (HircStart == INDEX_NONE || Song.Num() < ScreenBankTypeOffset + 4 || FMemory::Memcmp(Song.GetData(), "BKHD", 4) != 0)
		{
			return false;
		}
		const TSWwise::FObject* Event = Objects.FindByPredicate([](const TSWwise::FObject& Object) { return Object.Type == 4; });
		if (!Event || Objects.FilterByPredicate([](const TSWwise::FObject& Object) { return Object.Type == 4; }).Num() != 1)
		{
			return false;
		}
		// The event's first action: a Play (type 0x0403) whose target is the song's sound.
		uint32 ActionId = 0;
		FMemory::Memcpy(&ActionId, Song.GetData() + Event->Start + Event->Size - 4, sizeof(ActionId));
		const TSWwise::FObject* Action =
			Objects.FindByPredicate([&Song, ActionId](const TSWwise::FObject& Object) { return Object.Type == 3 && Object.GetId(Song) == ActionId; });
		if (!Action || Action->Size < 5 + 10 || Song[Action->Start + 5 + 4] != 0x03 || Song[Action->Start + 5 + 5] != 0x04)
		{
			return false;
		}
		uint32 SoundId = 0;
		FMemory::Memcpy(&SoundId, Song.GetData() + Action->Start + 5 + 6, sizeof(SoundId));
		if (!Objects.ContainsByPredicate([&Song, SoundId](const TSWwise::FObject& Object)
			{ return Object.Type == 2 && Object.GetId(Song) == SoundId && Object.Size > 5 + ScreenSoundSourceEnd; }))
		{
			return false;
		}

		TMap<uint32, uint32> NewIds;
		for (const TSWwise::FObject& Object : Objects)
		{
			const uint32 Id = Object.GetId(Song);
			NewIds.Add(Id, TSWwise::IdFromName(FString::Printf(TEXT("TS_Screen_%08x"), Id)));
		}
		const uint32 BankId = TSWwise::IdFromName(TEXT("TS_Screen_Bank"));
		const uint32 SourceId = TSWwise::IdFromName(TEXT("TS_Screen_Input"));

		auto Remap = [&NewIds](TArray<uint8>& Body)
		{
			for (int32 i = 0; i + 4 <= Body.Num(); ++i)
			{
				uint32 Value = 0;
				FMemory::Memcpy(&Value, Body.GetData() + i, sizeof(Value));
				if (const uint32* NewId = NewIds.Find(Value))
				{
					FMemory::Memcpy(Body.GetData() + i, NewId, sizeof(*NewId));
					i += 3;
				}
			}
		};
		auto AppendU32 = [](TArray<uint8>& Out, uint32 Value) { Out.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value)); };
		auto AppendObject = [&AppendU32](TArray<uint8>& Out, uint8 Type, const TArray<uint8>& Body)
		{
			Out.Add(Type);
			AppendU32(Out, Body.Num());
			Out.Append(Body);
		};

		TArray<uint8> Hirc;
		uint32 Count = 0;
		for (const TSWwise::FObject& Object : Objects)
		{
			const uint32 Id = Object.GetId(Song);
			TArray<uint8> Body(Song.GetData() + Object.Start + 5, Object.Size - 5);
			Remap(Body);
			if (Object.Type == 2 && Id == SoundId)
			{
				// Plugin settings for the Audio Input source: plugin id, no parameters (the plugin's defaults), no media,
				// RTPCs or states.
				TArray<uint8> Settings;
				AppendU32(Settings, SourceId);
				AppendU32(Settings, TSWwise::AudioInputPluginId);
				AppendU32(Settings, 0);
				Settings.AddZeroed(7);
				AppendObject(Hirc, 17, Settings);
				++Count;

				TArray<uint8> Source;
				AppendU32(Source, NewIds[SoundId]);
				AppendU32(Source, TSWwise::AudioInputPluginId);
				Source.Add(0);
				AppendU32(Source, SourceId);
				AppendU32(Source, 0);
				AppendU32(Source, 0);
				Source.Add(0);
				// A plugin source's parameter size (its parameters are in the settings object).
				AppendU32(Source, 0);
				Source.Append(Body.GetData() + ScreenSoundSourceEnd, Body.Num() - ScreenSoundSourceEnd);
				Body = MoveTemp(Source);
			}
			else if (Object.Type == 3 && Body.Num() >= 8)
			{
				// An action ends with the id and type of the bank holding its target.
				FMemory::Memcpy(Body.GetData() + Body.Num() - 8, &BankId, sizeof(BankId));
				FMemory::Memzero(Body.GetData() + Body.Num() - 4, 4);
			}
			AppendObject(Hirc, Object.Type, Body);
			++Count;
		}

		uint32 HeaderSize = 0;
		FMemory::Memcpy(&HeaderSize, Song.GetData() + 4, sizeof(HeaderSize));
		if (HeaderSize < ScreenBankTypeOffset + 4 - 8 || 8 + (int64)HeaderSize > Song.Num())
		{
			return false;
		}
		TArray<uint8> Header(Song.GetData() + 8, HeaderSize);
		FMemory::Memcpy(Header.GetData() + ScreenBankIdOffset - 8, &BankId, sizeof(BankId));
		FMemory::Memzero(Header.GetData() + ScreenBankTypeOffset - 8, 4);

		OutBank.Reset();
		OutBank.Append(reinterpret_cast<const uint8*>("BKHD"), 4);
		AppendU32(OutBank, Header.Num());
		OutBank.Append(Header);
		OutBank.Append(reinterpret_cast<const uint8*>("HIRC"), 4);
		AppendU32(OutBank, Hirc.Num() + 4);
		AppendU32(OutBank, Count);
		OutBank.Append(Hirc);
		OutEventId = NewIds[Event->GetId(Song)];
		return true;
	}

	// UAkComponent::UpdateGameObjectPosition, from the game's AkAudio DLL.
	void (*GetScreenUpdatePosition())(UObject*)
	{
		static void (*Function)(UObject*) = []() -> void (*)(UObject*)
		{
#if PLATFORM_WINDOWS
			for (const TCHAR* Dll : {TEXT("FactoryGameSteam-AkAudio-Win64-Shipping.dll"), TEXT("FactoryGameEGS-AkAudio-Win64-Shipping.dll")})
			{
				if (void* Module = GetModuleHandleW(Dll))
				{
					return reinterpret_cast<void (*)(UObject*)>(FPlatformProcess::GetDllExport(Module, TEXT("?UpdateGameObjectPosition@UAkComponent@@UEAAXXZ")));
				}
			}
#endif
			return nullptr;
		}();
		return Function;
	}
}

bool TSWwiseScreens::EnsureReady()
{
	if (bScreensTried)
	{
		return bScreensReady;
	}
	bScreensTried = true;
	if (!TSWwise::GetCalls().IsComplete() || !GetScreenUpdatePosition() || !FindObject<UClass>(nullptr, ScreenAkComponentClass))
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Wwise screens: the game's Wwise functions or AkComponent aren't all there; screens play through Unreal"));
		return false;
	}
	if (!TSWwise::RegisterBuiltInPlugin(TSWwise::AudioInputPluginId, TEXT("Wwise screens")))
	{
		UE_LOG(LogTelescreen, Log, TEXT("Wwise screens: unavailable; screens play through Unreal"));
		return false;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectContentDir() / TEXT("WwiseAudio") / TEXT("Event")), TEXT("Play_Boombox_*.bnk"), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File);
		if (Name.Contains(TEXT("_UI_")) || Name.Contains(TEXT("Animation")))
		{
			continue;
		}
		TArray<uint8> Song;
		TArray<uint8> Bank;
		if (!FFileHelper::LoadFileToArray(Song, *File) || !BuildScreenBank(Song, Bank, ScreenEventId))
		{
			continue;
		}
		TSWwise::PrepareBank(Bank, TEXT("Wwise screens"));
		UE_LOG(LogTelescreen, Log, TEXT("Wwise screens: built from the game's '%s'"), *Name);
		if (!TSWwise::LoadBank(Bank, TEXT("Wwise screens")))
		{
			break;
		}
		if (!bScreenSourceRegistered)
		{
			IModularFeatures::Get().RegisterModularFeature(ISFModWwiseAudioInputSource::GetName(), &ScreenAudioSource);
			bScreenSourceRegistered = true;
		}
		bScreensReady = TSWwise::SetAudioInputCallbacks(&DispatchScreenAudioInput, &DispatchScreenAudioFormat);
		break;
	}
	UE_LOG(LogTelescreen, Log, TEXT("Wwise screens: %s (%d Boom Box song bank(s) found)"),
		bScreensReady ? TEXT("ready; screens play through the game's audio engine") : TEXT("unavailable; screens play through Unreal"), Files.Num());
	return bScreensReady;
}

void TSWwiseScreens::Shutdown()
{
	if (bScreenSourceRegistered)
	{
		IModularFeatures::Get().UnregisterModularFeature(ISFModWwiseAudioInputSource::GetName(), &ScreenAudioSource);
		bScreenSourceRegistered = false;
	}
}

USceneComponent* TSWwiseScreens::CreateEmitter(USceneComponent* Parent)
{
	UClass* Class = FindObject<UClass>(nullptr, ScreenAkComponentClass);
	AActor* Owner = Parent ? Parent->GetOwner() : nullptr;
	if (!Class || !Owner)
	{
		return nullptr;
	}
	USceneComponent* Emitter = NewObject<USceneComponent>(Owner, Class, TEXT("TelescreenAk"));
	Emitter->SetMobility(EComponentMobility::Movable);
	Emitter->SetupAttachment(Parent);
	Emitter->RegisterComponent();
	return Emitter;
}

void TSWwiseScreens::SetAttenuationScale(USceneComponent* Emitter, float Scale)
{
	UFunction* Function = Emitter ? Emitter->FindFunction(TEXT("SetAttenuationScalingFactor")) : nullptr;
	if (!Function)
	{
		return;
	}
	uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
	FMemory::Memzero(Params, FMath::Max<int32>(Function->ParmsSize, 1));
	if (FFloatProperty* Value = CastField<FFloatProperty>(Function->PropertyLink))
	{
		Value->SetPropertyValue_InContainer(Params, Scale);
		Emitter->ProcessEvent(Function, Params);
	}
}

uint32 TSWwiseScreens::Play(const TSharedPtr<FTSAudioReader, ESPMode::ThreadSafe>& Reader, USceneComponent* Emitter)
{
	if (!EnsureReady() || !Reader || !Emitter)
	{
		return 0;
	}
	TSharedPtr<FScreenFeed> Feed = MakeShared<FScreenFeed>();
	Feed->Reader = Reader;
	Feed->SampleRate = int32(Reader->GetSampleRate());
	Feed->Channels = FMath::Clamp(int32(Reader->GetChannels()), 1, 2);
	UpdatePosition(Emitter);
	// The callbacks wait on the lock, so the feed is registered before Wwise first asks for its format.
	FScopeLock Lock(&ScreenFeedsLock);
	const uint32 PlayingId = TSWwise::GetCalls().PostEvent(ScreenEventId, reinterpret_cast<uint64>(Emitter), ScreenEndOfEventFlag, &HandleScreenEventCallback, nullptr, 0, nullptr, 0);
	if (PlayingId != 0)
	{
		ScreenFeeds.Add(PlayingId, Feed);
	}
	return PlayingId;
}

void TSWwiseScreens::Stop(uint32 PlayingId)
{
	if (PlayingId == 0)
	{
		return;
	}
	{
		FScopeLock Lock(&ScreenFeedsLock);
		ScreenFeeds.Remove(PlayingId);
	}
	TSWwise::GetCalls().StopPlayingID(PlayingId, 0, 4);
}

void TSWwiseScreens::SetGain(uint32 PlayingId, float Gain)
{
	FScopeLock Lock(&ScreenFeedsLock);
	if (const TSharedPtr<FScreenFeed>* Found = ScreenFeeds.Find(PlayingId))
	{
		(*Found)->Gain = Gain;
	}
}

bool TSWwiseScreens::HasEnded(uint32 PlayingId)
{
	FScopeLock Lock(&ScreenFeedsLock);
	const TSharedPtr<FScreenFeed>* Found = ScreenFeeds.Find(PlayingId);
	return !Found || (*Found)->bEnded.load();
}

void TSWwiseScreens::UpdatePosition(USceneComponent* Emitter)
{
	if (void (*Update)(UObject*) = GetScreenUpdatePosition(); Update && IsValid(Emitter))
	{
		Update(Emitter);
	}
}
