#include "TSConfig.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Telescreen.h"

#define LOCTEXT_NAMESPACE "Telescreen"

const FString UTSConfig::MaxActiveScreensKey = TEXT("MaxActiveScreens");
const FString UTSConfig::PlayDistanceKey = TEXT("PlayDistance");
const FString UTSConfig::MaxVideoHeightKey = TEXT("MaxVideoHeight");
const FString UTSConfig::VolumeKey = TEXT("Volume");
const FString UTSConfig::AudioRangeKey = TEXT("AudioRange");
const FString UTSConfig::MuteInBackgroundKey = TEXT("MuteInBackground");
const FString UTSConfig::AutoLoadKey = TEXT("AutoLoad");
const FString UTSConfig::QueuePolicyKey = TEXT("QueuePolicy");
const FString UTSConfig::AllowedSitesKey = TEXT("AllowedSites");
const FString UTSConfig::AllowAnySiteKey = TEXT("AllowAnySite");
const FString UTSConfig::MaxVideoMinutesKey = TEXT("MaxVideoMinutes");
const FString UTSConfig::QueueAddsPerMinuteKey = TEXT("QueueAddsPerMinute");
const FString UTSConfig::PipWidthKey = TEXT("PipWidth");
const FString UTSConfig::PipCornerKey = TEXT("PipCorner");
const FString UTSConfig::PipAudio2DKey = TEXT("PipAudio2D");
const FString UTSConfig::MirrorXKey = TEXT("MirrorX");
const FString UTSConfig::MirrorYKey = TEXT("MirrorY");

namespace
{
	const TCHAR* DefaultAllowedSites = TEXT("youtube.com, youtu.be, twitch.tv, vimeo.com");

	FConfigId MakeConfigId()
	{
		FConfigId Id;
		Id.ModReference = TEXT("Telescreen");
		return Id;
	}

	UConfigProperty* FindInSection(const UConfigPropertySection* Section, const FString& Key)
	{
		if (!Section)
		{
			return nullptr;
		}
		if (const TObjectPtr<UConfigProperty>* Found = Section->SectionProperties.Find(Key))
		{
			return Found->Get();
		}
		for (const auto& Pair : Section->SectionProperties)
		{
			if (UConfigProperty* Nested = FindInSection(Cast<UConfigPropertySection>(Pair.Value.Get()), Key))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	// The live value of a setting, or null if the configuration isn't registered yet.
	UConfigProperty* FindProperty(const UObject* WorldContext, const FString& Key)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
		return FindInSection(Manager ? Manager->GetConfigurationRootSection(MakeConfigId()) : nullptr, Key);
	}
}

UTSConfig::UTSConfig()
{
	ConfigId = MakeConfigId();
	DisplayName = LOCTEXT("ConfigName", "Telescreen");
	Description = LOCTEXT("ConfigDescription", "Screens for synced videos, browsers, windows and remote desktops.");
	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	auto AddSection = [this](const FString& Key, const FText& Name) -> UConfigPropertySection*
	{
		UConfigPropertySection* Section = CreateDefaultSubobject<UConfigPropertySection>(*(Key + TEXT("Section")));
		Section->DisplayName = Name;
		RootSection->SectionProperties.Add(Key, Section);
		return Section;
	};
	auto AddBool = [this](UConfigPropertySection* Section, const FString& Key, const FText& Name, const FText& Tip, bool Default)
	{
		UConfigPropertyBool* P = CreateDefaultSubobject<UConfigPropertyBool>(*Key);
		P->DisplayName = Name;
		P->Tooltip = Tip;
		P->DefaultValue = P->Value = Default;
		Section->SectionProperties.Add(Key, P);
	};
	auto AddFloat = [this](UConfigPropertySection* Section, const FString& Key, const FText& Name, const FText& Tip, float Default)
	{
		UConfigPropertyFloat* P = CreateDefaultSubobject<UConfigPropertyFloat>(*Key);
		P->DisplayName = Name;
		P->Tooltip = Tip;
		P->DefaultValue = P->Value = Default;
		Section->SectionProperties.Add(Key, P);
	};
	auto AddInt = [this](UConfigPropertySection* Section, const FString& Key, const FText& Name, const FText& Tip, int32 Default)
	{
		UConfigPropertyInteger* P = CreateDefaultSubobject<UConfigPropertyInteger>(*Key);
		P->DisplayName = Name;
		P->Tooltip = Tip;
		P->DefaultValue = P->Value = Default;
		Section->SectionProperties.Add(Key, P);
	};
	auto AddString = [this](UConfigPropertySection* Section, const FString& Key, const FText& Name, const FText& Tip, const FString& Default)
	{
		UConfigPropertyString* P = CreateDefaultSubobject<UConfigPropertyString>(*Key);
		P->DisplayName = Name;
		P->Tooltip = Tip;
		P->DefaultValue = P->Value = Default;
		Section->SectionProperties.Add(Key, P);
	};

	UConfigPropertySection* Performance = AddSection(TEXT("Performance"), LOCTEXT("Performance", "Performance"));
	AddInt(Performance, MaxActiveScreensKey, LOCTEXT("MaxActiveScreens", "Screens playing at once"),
		LOCTEXT("MaxActiveScreensTip", "How many screens near you play video at the same time. The nearest ones play; the rest show their status."), 4);
	AddFloat(Performance, PlayDistanceKey, LOCTEXT("PlayDistance", "Play distance (m)"),
		LOCTEXT("PlayDistanceTip", "Screens further away than this pause on your PC (they stay in sync and resume when you come back)."), 150.f);
	AddInt(Performance, MaxVideoHeightKey, LOCTEXT("MaxVideoHeight", "Video quality (lines)"),
		LOCTEXT("MaxVideoHeightTip", "Highest video resolution to fetch, e.g. 480, 720 or 1080. Higher looks sharper on big screens and costs more CPU and bandwidth."), 720);

	UConfigPropertySection* Sound = AddSection(TEXT("Audio"), LOCTEXT("Audio", "Audio"));
	AddFloat(Sound, VolumeKey, LOCTEXT("Volume", "Screen volume"),
		LOCTEXT("VolumeTip", "Volume of all screens, from 0 to 1, on top of the game's Master volume."), 1.f);
	AddFloat(Sound, AudioRangeKey, LOCTEXT("AudioRange", "Hearing range"),
		LOCTEXT("AudioRangeTip", "Multiplies how far screens can be heard; bigger screens already carry further."), 1.f);
	AddBool(Sound, MuteInBackgroundKey, LOCTEXT("MuteInBackground", "Mute when the game is in the background"),
		LOCTEXT("MuteInBackgroundTip", "Silence screens while you're in another window. Videos keep playing in sync."), true);

	UConfigPropertySection* Pip = AddSection(TEXT("HudPicture"), LOCTEXT("HudPicture", "Picture on the HUD"));
	AddFloat(Pip, PipWidthKey, LOCTEXT("PipWidth", "Width"),
		LOCTEXT("PipWidthTip", "Width of a screen shown on the HUD (\"Show on HUD\" in the screen's panel)."), 420.f);
	AddInt(Pip, PipCornerKey, LOCTEXT("PipCorner", "Corner"),
		LOCTEXT("PipCornerTip", "0: bottom right; 1: bottom left; 2: top right; 3: top left."), 0);
	AddBool(Pip, PipAudio2DKey, LOCTEXT("PipAudio2D", "Hear it everywhere"),
		LOCTEXT("PipAudio2DTip", "While a screen is on the HUD, play its sound at full volume wherever you are, instead of from the screen."), true);

	UConfigPropertySection* Privacy = AddSection(TEXT("Privacy"), LOCTEXT("Privacy", "Privacy"));
	AddInt(Privacy, AutoLoadKey, LOCTEXT("AutoLoad", "Load videos other players add"),
		LOCTEXT("AutoLoadTip", "Loading a video contacts its website from your PC. 0: only from the allowed sites below; 1: from any site; 2: never (you see the title only)."),
		int32(ETSAutoLoad::AllowedSitesOnly));

	UConfigPropertySection* Hosting = AddSection(TEXT("Hosting"), LOCTEXT("Hosting", "When hosting"));
	AddInt(Hosting, QueuePolicyKey, LOCTEXT("QueuePolicy", "Who can change screen videos"),
		LOCTEXT("QueuePolicyTip", "0: anyone; 1: only the host. Applies to worlds this PC hosts; no effect on dedicated servers yet."), int32(ETSQueuePolicy::Anyone));
	AddString(Hosting, AllowedSitesKey, LOCTEXT("AllowedSites", "Allowed sites"),
		LOCTEXT("AllowedSitesTip", "Comma-separated sites videos can be added from. Also used by the privacy setting above."), DefaultAllowedSites);
	AddBool(Hosting, AllowAnySiteKey, LOCTEXT("AllowAnySite", "Allow videos from any site"),
		LOCTEXT("AllowAnySiteTip", "Let players add links from sites that aren't in the list (each player's privacy setting still applies)."), false);
	AddInt(Hosting, MaxVideoMinutesKey, LOCTEXT("MaxVideoMinutes", "Longest video (minutes)"),
		LOCTEXT("MaxVideoMinutesTip", "Reject longer videos. 0 means no limit."), 0);
	AddInt(Hosting, QueueAddsPerMinuteKey, LOCTEXT("QueueAddsPerMinute", "Videos a player can add per minute"),
		LOCTEXT("QueueAddsPerMinuteTip", "Limits queue spam."), 10);

	UConfigPropertySection* Diagnostics = AddSection(TEXT("Diagnostics"), LOCTEXT("Diagnostics", "Diagnostics"));
	AddBool(Diagnostics, MirrorXKey, LOCTEXT("MirrorX", "Mirror picture left-right"),
		LOCTEXT("MirrorXTip", "Only if screens show the picture mirrored."), false);
	AddBool(Diagnostics, MirrorYKey, LOCTEXT("MirrorY", "Flip picture upside down"),
		LOCTEXT("MirrorYTip", "Only if screens show the picture upside down."), false);
}

namespace
{
	const TCHAR* SMLPropertyPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/");

	UClass* LoadEditorClass(const UClass* NativeClass)
	{
		const FString Name = TEXT("BP_") + NativeClass->GetName();
		const FString Path = FString::Printf(TEXT("%s%s.%s_C"), SMLPropertyPath, *Name, *Name);
		UClass* Loaded = LoadClass<UConfigProperty>(nullptr, *Path);
		if (!Loaded || !Loaded->IsChildOf(NativeClass))
		{
			UE_LOG(LogTelescreen, Warning, TEXT("Config: SML editor class %s not found; that setting won't be editable in the Mods menu"), *Path);
			return nullptr;
		}
		return Loaded;
	}

	UConfigProperty* CloneAs(const UConfigProperty* Source, UClass* EditorClass, UObject* Outer)
	{
		UConfigProperty* Clone = NewObject<UConfigProperty>(Outer, EditorClass, NAME_None, RF_Public);
		for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
		{
			It->CopyCompleteValue_InContainer(Clone, Source);
		}
		return Clone;
	}

	const TMap<FString, TPair<float, float>>& GetNumberRanges()
	{
		static const TMap<FString, TPair<float, float>> Ranges = {
			{UTSConfig::MaxActiveScreensKey, {0.f, 32.f}},
			{UTSConfig::PlayDistanceKey, {10.f, 2000.f}},
			{UTSConfig::MaxVideoHeightKey, {144.f, 2160.f}},
			{UTSConfig::VolumeKey, {0.f, 1.f}},
			{UTSConfig::AudioRangeKey, {0.1f, 10.f}},
			{UTSConfig::AutoLoadKey, {0.f, 2.f}},
			{UTSConfig::PipWidthKey, {160.f, 1600.f}},
			{UTSConfig::PipCornerKey, {0.f, 3.f}},
			{UTSConfig::QueuePolicyKey, {0.f, 1.f}},
			{UTSConfig::MaxVideoMinutesKey, {0.f, 100000.f}},
			{UTSConfig::QueueAddsPerMinuteKey, {1.f, 1000.f}},
		};
		return Ranges;
	}

	void ApplyNumberRange(const FString& Key, UConfigProperty* Property)
	{
		const TPair<float, float>* Range = GetNumberRanges().Find(Key);
		if (UCP_Float* Float = Cast<UCP_Float>(Property); Float && Range)
		{
			Float->MinValue = Range->Key;
			Float->MaxValue = Range->Value;
		}
		else if (UCP_Integer* Integer = Cast<UCP_Integer>(Property); Integer && Range)
		{
			Integer->MinValue = FMath::RoundToInt(Range->Key);
			Integer->MaxValue = FMath::RoundToInt(Range->Value);
		}
	}

	UConfigPropertySection* ConvertSection(const UConfigPropertySection* OldSection, UClass* SectionClass, UObject* Outer)
	{
		UConfigPropertySection* NewSection = Cast<UConfigPropertySection>(CloneAs(OldSection, SectionClass, Outer));
		NewSection->SectionProperties.Reset();
		if (UCP_Section* EditorSection = Cast<UCP_Section>(NewSection))
		{
			EditorSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
		}
		for (const auto& Pair : OldSection->SectionProperties)
		{
			UConfigProperty* Old = Pair.Value;
			if (!Old)
			{
				continue;
			}
			if (const UConfigPropertySection* Child = Cast<UConfigPropertySection>(Old))
			{
				NewSection->SectionProperties.Add(Pair.Key, ConvertSection(Child, SectionClass, NewSection));
			}
			else if (UClass* EditorClass = LoadEditorClass(Old->GetClass()))
			{
				UConfigProperty* Clone = CloneAs(Old, EditorClass, NewSection);
				ApplyNumberRange(Pair.Key, Clone);
				NewSection->SectionProperties.Add(Pair.Key, Clone);
			}
			else
			{
				NewSection->SectionProperties.Add(Pair.Key, Old);
			}
		}
		return NewSection;
	}
}

void UTSConfig::UseSMLEditorClasses()
{
	UTSConfig* Defaults = GetMutableDefault<UTSConfig>();
	UClass* SectionClass = Defaults->RootSection ? LoadEditorClass(UConfigPropertySection::StaticClass()) : nullptr;
	if (!SectionClass || Defaults->RootSection->GetClass() == SectionClass)
	{
		return;
	}
	Defaults->RootSection = ConvertSection(Defaults->RootSection, SectionClass, Defaults);
	UE_LOG(LogTelescreen, Log, TEXT("Config: using SML's editor widgets"));
}

bool UTSConfig::GetBool(const UObject* WorldContext, const FString& Key, bool Fallback)
{
	const UConfigPropertyBool* P = Cast<UConfigPropertyBool>(FindProperty(WorldContext, Key));
	return P ? P->Value : Fallback;
}

float UTSConfig::GetFloat(const UObject* WorldContext, const FString& Key, float Fallback)
{
	const UConfigPropertyFloat* P = Cast<UConfigPropertyFloat>(FindProperty(WorldContext, Key));
	return P ? P->Value : Fallback;
}

int32 UTSConfig::GetInt(const UObject* WorldContext, const FString& Key, int32 Fallback)
{
	const UConfigPropertyInteger* P = Cast<UConfigPropertyInteger>(FindProperty(WorldContext, Key));
	return P ? P->Value : Fallback;
}

FString UTSConfig::GetString(const UObject* WorldContext, const FString& Key, const FString& Fallback)
{
	const UConfigPropertyString* P = Cast<UConfigPropertyString>(FindProperty(WorldContext, Key));
	return P ? P->Value : Fallback;
}

bool UTSConfig::IsAllowedSite(const UObject* WorldContext, const FString& Url)
{
	FString Rest;
	if (!Url.Split(TEXT("://"), nullptr, &Rest))
	{
		return false;
	}
	FString Host = Rest;
	Rest.Split(TEXT("/"), &Host, nullptr);
	Host.Split(TEXT(":"), &Host, nullptr);
	Host = Host.ToLower();
	TArray<FString> Sites;
	GetString(WorldContext, AllowedSitesKey, DefaultAllowedSites).ParseIntoArray(Sites, TEXT(","));
	for (FString Site : Sites)
	{
		Site = Site.TrimStartAndEnd().ToLower();
		if (!Site.IsEmpty() && (Host == Site || Host.EndsWith(TEXT(".") + Site)))
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
