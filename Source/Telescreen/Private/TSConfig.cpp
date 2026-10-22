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
const FString UTSConfig::AllowStreamingKey = TEXT("AllowStreaming");
const FString UTSConfig::MaxStreamKbpsKey = TEXT("MaxStreamKbps");
const FString UTSConfig::StreamKbpsKey = TEXT("StreamKbps");
const FString UTSConfig::StreamFpsKey = TEXT("StreamFps");
const FString UTSConfig::ReceiveStreamsKey = TEXT("ReceiveStreams");
const FString UTSConfig::CurtainKeyKey = TEXT("CurtainKey");
const FString UTSConfig::StreamDisclaimerKey = TEXT("StreamDisclaimerAccepted");
const FString UTSConfig::AllowBrowserKey = TEXT("AllowBrowser");
const FString UTSConfig::BrowserRoleKey = TEXT("BrowserRole");
const FString UTSConfig::WindowRoleKey = TEXT("WindowRole");
const FString UTSConfig::TrustedPlayersKey = TEXT("TrustedPlayers");
const FString UTSConfig::AllowTakeOverKey = TEXT("AllowTakeOver");
const FString UTSConfig::MaxControlledScreensKey = TEXT("MaxControlledScreens");
const FString UTSConfig::WindowAudioKey = TEXT("WindowAudio");
const FString UTSConfig::WindowCursorKey = TEXT("WindowCursor");
const FString UTSConfig::WindowDisclaimerKey = TEXT("WindowDisclaimerAccepted");
const FString UTSConfig::BrowserHomeKey = TEXT("BrowserHome");
const FString UTSConfig::KeepBrowserLoginsKey = TEXT("KeepBrowserLogins");
const FString UTSConfig::FocusKeyKey = TEXT("FocusKey");
const FString UTSConfig::ScreensListKeyKey = TEXT("ScreensListKey");
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
	Description = LOCTEXT("ConfigDescription", "Screens for synced videos, a built-in browser and shared windows.");
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

	UConfigPropertySection* BrowserSection = AddSection(TEXT("Browser"), LOCTEXT("BrowserSection", "Browser and window"));
	AddString(BrowserSection, BrowserHomeKey, LOCTEXT("BrowserHome", "Browser start page"),
		LOCTEXT("BrowserHomeTip", "Page the built-in browser opens when you start it without a link."), TEXT("https://duckduckgo.com"));
	AddBool(BrowserSection, KeepBrowserLoginsKey, LOCTEXT("KeepBrowserLogins", "Keep browser logins"),
		LOCTEXT("KeepBrowserLoginsTip", "Remember sign-ins and cookies between sessions. Off: every browser starts clean. Anything you're signed in to is visible to players watching the screen."), false);
	AddInt(BrowserSection, StreamKbpsKey, LOCTEXT("StreamKbps", "Stream quality (kbps)"),
		LOCTEXT("StreamKbpsTip", "Video bitrate when you share your browser with other players. 2500 is sharp at 720p; lower it if your upload is slow."), 2500);
	AddInt(BrowserSection, StreamFpsKey, LOCTEXT("StreamFps", "Stream frame rate"),
		LOCTEXT("StreamFpsTip", "Frames per second of the shared browser: 30 or 60."), 30);
	AddInt(BrowserSection, ReceiveStreamsKey, LOCTEXT("ReceiveStreams", "Watch other players' browsers"),
		LOCTEXT("ReceiveStreamsTip", "0: show them automatically; 1: never (you see who is using the screen only)."), 0);
	AddString(BrowserSection, CurtainKeyKey, LOCTEXT("CurtainKey", "Privacy curtain key"),
		LOCTEXT("CurtainKeyTip", "Blanks your shared browser for everyone (you still see it) and back. Key name, e.g. F10."), TEXT("F10"));
	AddBool(BrowserSection, StreamDisclaimerKey, LOCTEXT("StreamDisclaimer", "I understand that shared browsers are visible to everyone"),
		LOCTEXT("StreamDisclaimerTip", "Set when you accept the sharing notice the first time you open the browser. Turn it off to see the notice again."), false);
	AddBool(BrowserSection, WindowAudioKey, LOCTEXT("WindowAudio", "Share a window's sound"),
		LOCTEXT("WindowAudioTip", "Send the shared window's own sound to the screen (needs Windows 11 or Windows 10 21H2 and later). You keep hearing it from the window itself."), true);
	AddBool(BrowserSection, WindowCursorKey, LOCTEXT("WindowCursor", "Show the mouse in a shared window"),
		LOCTEXT("WindowCursorTip", "Include your mouse pointer in the picture of a shared window."), true);
	AddBool(BrowserSection, WindowDisclaimerKey, LOCTEXT("WindowDisclaimer", "I understand that shared windows are visible to everyone"),
		LOCTEXT("WindowDisclaimerTip", "Set when you accept the notice the first time you share a window. Turn it off to see it again."), false);
	AddString(BrowserSection, FocusKeyKey, LOCTEXT("FocusKey", "Window focus key"),
		LOCTEXT("FocusKeyTip", "Switches the screen window between using the browser (mouse and keyboard go to it) and playing (the window stays as a picture). Key name, e.g. F8."), TEXT("F8"));
	AddString(BrowserSection, ScreensListKeyKey, LOCTEXT("ScreensListKey", "Screens list key"),
		LOCTEXT("ScreensListKeyTip", "Opens the list of every screen in the world. Key name, e.g. F9."), TEXT("F9"));

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
		LOCTEXT("QueuePolicyTip", "0: anyone; 1: only the host (on a dedicated server: its admins)."), int32(ETSQueuePolicy::Anyone));
	AddString(Hosting, AllowedSitesKey, LOCTEXT("AllowedSites", "Allowed sites"),
		LOCTEXT("AllowedSitesTip", "Comma-separated sites videos can be added from. Also used by the privacy setting above."), DefaultAllowedSites);
	AddBool(Hosting, AllowAnySiteKey, LOCTEXT("AllowAnySite", "Allow videos from any site"),
		LOCTEXT("AllowAnySiteTip", "Let players add links from sites that aren't in the list (each player's privacy setting still applies)."), false);
	AddBool(Hosting, AllowStreamingKey, LOCTEXT("AllowStreaming", "Allow sharing browsers with other players"),
		LOCTEXT("AllowStreamingTip", "Let a player's browser be streamed to everyone through this world's connection. Off: only the player using it sees it."), true);
	AddInt(Hosting, MaxStreamKbpsKey, LOCTEXT("MaxStreamKbps", "Most each stream may use (kbps)"),
		LOCTEXT("MaxStreamKbpsTip", "The server drops stream data above this rate."), 8000);
	AddBool(Hosting, AllowBrowserKey, LOCTEXT("AllowBrowser", "Allow the built-in browser"),
		LOCTEXT("AllowBrowserTip", "Let players use the built-in browser on screens."), true);
	AddInt(Hosting, BrowserRoleKey, LOCTEXT("BrowserRole", "Who can use the browser"),
		LOCTEXT("BrowserRoleTip", "0: everyone; 1: trusted players (list below) and the host; 2: only the host (on a dedicated server: its admins)."),
		int32(ETSRole::Everyone));
	AddInt(Hosting, WindowRoleKey, LOCTEXT("WindowRole", "Who can share a window"),
		LOCTEXT("WindowRoleTip", "Sharing a window shows something from that player's desktop to everyone. 0: everyone; 1: trusted players and the host; 2: only the host or admins."),
		int32(ETSRole::Trusted));
	AddString(Hosting, TrustedPlayersKey, LOCTEXT("TrustedPlayers", "Trusted players"),
		LOCTEXT("TrustedPlayersTip", "Comma-separated player names for the \"trusted players\" choices above."), FString());
	AddBool(Hosting, AllowTakeOverKey, LOCTEXT("AllowTakeOver", "Players can take over a screen in use"),
		LOCTEXT("AllowTakeOverTip", "Off: only the player using a screen (or the host and admins) can stop it; others wait until it's free."), true);
	AddInt(Hosting, MaxControlledScreensKey, LOCTEXT("MaxControlledScreens", "Screens in use at once"),
		LOCTEXT("MaxControlledScreensTip", "Most screens showing a browser or window at the same time in this world. Each player uses one at a time."), 4);
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
			{UTSConfig::StreamKbpsKey, {300.f, 20000.f}},
			{UTSConfig::StreamFpsKey, {10.f, 60.f}},
			{UTSConfig::ReceiveStreamsKey, {0.f, 1.f}},
			{UTSConfig::MaxStreamKbpsKey, {500.f, 100000.f}},
			{UTSConfig::PipCornerKey, {0.f, 3.f}},
			{UTSConfig::QueuePolicyKey, {0.f, 1.f}},
			{UTSConfig::BrowserRoleKey, {0.f, 2.f}},
			{UTSConfig::WindowRoleKey, {0.f, 2.f}},
			{UTSConfig::MaxControlledScreensKey, {1.f, 64.f}},
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

void UTSConfig::SetBool(const UObject* WorldContext, const FString& Key, bool Value)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	UConfigPropertyBool* Property = Manager ? Cast<UConfigPropertyBool>(FindProperty(WorldContext, Key)) : nullptr;
	if (!Property)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Config: could not set '%s'; configuration not available"), *Key);
		return;
	}
	Property->Value = Value;
	Manager->MarkConfigurationDirty(MakeConfigId());
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
	// The host ends at the first of / ? # and drops any user@ part and :port.
	int32 End = Rest.Len();
	for (const TCHAR* Stop : {TEXT("/"), TEXT("?"), TEXT("#"), TEXT("\\")})
	{
		const int32 At = Rest.Find(Stop);
		if (At != INDEX_NONE)
		{
			End = FMath::Min(End, At);
		}
	}
	FString Host = Rest.Left(End);
	Host.Split(TEXT("@"), nullptr, &Host, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	Host.Split(TEXT(":"), &Host, nullptr);
	Host = Host.ToLower();
	if (Host.IsEmpty())
	{
		return false;
	}
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

bool UTSConfig::IsTrustedPlayer(const UObject* WorldContext, const FString& PlayerName)
{
	TArray<FString> Names;
	GetString(WorldContext, TrustedPlayersKey, FString()).ParseIntoArray(Names, TEXT(","));
	for (const FString& Name : Names)
	{
		if (!PlayerName.IsEmpty() && Name.TrimStartAndEnd().Equals(PlayerName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
