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
const FString UTSConfig::WwiseAudioKey = TEXT("WwiseAudio");
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
const FString UTSConfig::StreamHeightKey = TEXT("StreamHeight");
const FString UTSConfig::StreamEncoderKey = TEXT("StreamEncoder");
const FString UTSConfig::BrowserHeightKey = TEXT("BrowserHeight");
const FString UTSConfig::RemoteMatchScreenKey = TEXT("RemoteMatchScreen");
const FString UTSConfig::VideosFolderKey = TEXT("VideosFolder");
const FString UTSConfig::SubtitleModeKey = TEXT("SubtitleMode");
const FString UTSConfig::DirectKey = TEXT("DirectStreams");
const FString UTSConfig::DirectPortKey = TEXT("DirectPort");
const FString UTSConfig::DirectUpnpKey = TEXT("DirectUpnp");
const FString UTSConfig::AllowDirectKey = TEXT("AllowDirect");
const FString UTSConfig::SubtitleLanguageKey = TEXT("SubtitleLanguage");
const FString UTSConfig::StreamMyVideosKey = TEXT("StreamMyVideos");
const FString UTSConfig::MaxStreamHeightKey = TEXT("MaxStreamHeight");
const FString UTSConfig::MaxStreamFpsKey = TEXT("MaxStreamFps");
const FString UTSConfig::ReceiveStreamsKey = TEXT("ReceiveStreams");
const FString UTSConfig::CurtainKeyKey = TEXT("CurtainKey");
const FString UTSConfig::StreamDisclaimerKey = TEXT("StreamDisclaimerAccepted");
const FString UTSConfig::AllowBrowserKey = TEXT("AllowBrowser");
const FString UTSConfig::BrowserRoleKey = TEXT("BrowserRole");
const FString UTSConfig::WindowRoleKey = TEXT("WindowRole");
const FString UTSConfig::RemoteDesktopRoleKey = TEXT("RemoteDesktopRole");
const FString UTSConfig::ViewerInputBrowserKey = TEXT("ViewerInputBrowser");
const FString UTSConfig::ViewerInputWindowKey = TEXT("ViewerInputWindow");
const FString UTSConfig::ViewerInputRemoteKey = TEXT("ViewerInputRemote");
const FString UTSConfig::GrantMinutesKey = TEXT("GrantMinutes");
const FString UTSConfig::GrantDisclaimerKey = TEXT("GrantDisclaimerAccepted");
const FString UTSConfig::RemoteDisclaimerKey = TEXT("RemoteDisclaimerAccepted");
const FString UTSConfig::TrustedPlayersKey = TEXT("TrustedPlayers");
const FString UTSConfig::AllowTakeOverKey = TEXT("AllowTakeOver");
const FString UTSConfig::MaxControlledScreensKey = TEXT("MaxControlledScreens");
const FString UTSConfig::WindowAudioKey = TEXT("WindowAudio");
const FString UTSConfig::WindowCursorKey = TEXT("WindowCursor");
const FString UTSConfig::WindowDisclaimerKey = TEXT("WindowDisclaimerAccepted");
const FString UTSConfig::BrowserHomeKey = TEXT("BrowserHome");
const FString UTSConfig::SearchEngineKey = TEXT("SearchEngine");
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

	UConfigPropertySection* LocalVideos = AddSection(TEXT("LocalVideos"), LOCTEXT("LocalVideos", "Videos on this PC"));
	AddString(LocalVideos, VideosFolderKey, LOCTEXT("VideosFolder", "Videos folder"),
		LOCTEXT("VideosFolderTip", "Where your video files are (e.g. your Plex folder). When a screen plays a file that isn't at the same path on your PC, Telescreen looks for a file with the same name in this folder and its subfolders. Also set with the Videos tab's Change folder button."),
		FString());
	AddBool(LocalVideos, StreamMyVideosKey, LOCTEXT("StreamMyVideos", "Stream videos to players who can't play them"),
		LOCTEXT("StreamMyVideosTip", "When a video plays on your PC but another player can't play it (they don't have the file, or the link fails for them), your PC streams it to them like a shared window. Off: they see the title only."), true);

	UConfigPropertySection* Network = AddSection(TEXT("Network"), LOCTEXT("NetworkSection", "Direct connections"));
	Network->Tooltip = LOCTEXT("NetworkSectionTip", "Live streams normally go through the world host's connection. With direct connections on (for both the player sharing and the viewer), they go straight between your PCs instead, so the world host's upload isn't the limit.");
	AddBool(Network, DirectKey, LOCTEXT("DirectStreams", "Direct connections for streams"),
		LOCTEXT("DirectStreamsTip", "Send and receive live streams straight between your PC and other players' PCs (encrypted), when both of you turn this on and the world host allows it. Other players in the stream learn your public IP address, and your PC asks a public STUN server (Google, Cloudflare) for its own. Falls back to the world host's connection when a direct link fails."),
		false);
	AddInt(Network, DirectPortKey, LOCTEXT("DirectPort", "Direct connection port (UDP)"),
		LOCTEXT("DirectPortTip", "0: any free port. Set a number if you forward that UDP port on your router to this PC."), 0);
	AddBool(Network, DirectUpnpKey, LOCTEXT("DirectUpnp", "Open the port on the router (UPnP)"),
		LOCTEXT("DirectUpnpTip", "Ask the home router to open the direct connection port while the game runs (routers with UPnP on). Helps when a direct link can't be made otherwise."), true);

	UConfigPropertySection* SubtitleSection = AddSection(TEXT("Subtitles"), LOCTEXT("Subtitles", "Subtitles"));
	AddInt(SubtitleSection, SubtitleModeKey, LOCTEXT("SubtitleMode", "Show subtitles"),
		LOCTEXT("SubtitleModeTip", "0: off; 1: on the screen; 2: on your HUD (for the nearest screen you can hear); 3: both. Only on your PC. Also in each screen's Videos tab."), 0);
	AddString(SubtitleSection, SubtitleLanguageKey, LOCTEXT("SubtitleLanguage", "Subtitle language"),
		LOCTEXT("SubtitleLanguageTip", "Language picked first, as a code: en, de, fr, es, ja... Automatic captions are offered in the video's language and this one."), TEXT("en"));

	UConfigPropertySection* Sound = AddSection(TEXT("Audio"), LOCTEXT("Audio", "Audio"));
	AddFloat(Sound, VolumeKey, LOCTEXT("Volume", "Screen volume"),
		LOCTEXT("VolumeTip", "Volume of all screens, from 0 to 1, on top of the game's Master volume."), 1.f);
	AddFloat(Sound, AudioRangeKey, LOCTEXT("AudioRange", "Hearing range"),
		LOCTEXT("AudioRangeTip", "Multiplies how far screens can be heard; bigger screens already carry further."), 1.f);
	AddBool(Sound, MuteInBackgroundKey, LOCTEXT("MuteInBackground", "Mute when the game is in the background"),
		LOCTEXT("MuteInBackgroundTip", "Silence screens while you're in another window. Videos keep playing in sync."), true);
	AddBool(Sound, WwiseAudioKey, LOCTEXT("WwiseAudio", "Play through the game's audio engine"),
		LOCTEXT("WwiseAudioTip", "Screen sound goes through the game's own audio (rooms, reverb, walls, the game's volume sliders for the Boom Box). Off: Unreal plays it directly. Applies to sounds that start after the change."), true);

	// The player sharing a browser, window or remote desktop: it runs on their PC, and these are their choices.
	UConfigPropertySection* BrowserSection = AddSection(TEXT("Browser"), LOCTEXT("BrowserSection", "When you share (your PC)"));
	BrowserSection->Tooltip = LOCTEXT("BrowserSectionTip", "Your own browser, window or remote desktop, shown on a screen from your PC. The world host's rules (below) limit what you can share and give.");
	AddString(BrowserSection, BrowserHomeKey, LOCTEXT("BrowserHome", "Browser start page"),
		LOCTEXT("BrowserHomeTip", "Page the built-in browser opens when you start it without a link."), TEXT("https://duckduckgo.com"));
	AddString(BrowserSection, SearchEngineKey, LOCTEXT("SearchEngine", "Browser search engine"),
		LOCTEXT("SearchEngineTip", "Where words typed in the browser's address box are searched; %s is replaced by them. E.g. https://duckduckgo.com/?q=%s, https://www.google.com/search?q=%s, https://www.bing.com/search?q=%s, https://www.startpage.com/do/search?q=%s"),
		TEXT("https://duckduckgo.com/?q=%s"));
	AddBool(BrowserSection, KeepBrowserLoginsKey, LOCTEXT("KeepBrowserLogins", "Keep browser logins"),
		LOCTEXT("KeepBrowserLoginsTip", "Remember sign-ins and cookies between sessions. Off: every browser starts clean. Anything you're signed in to is visible to players watching the screen."), false);
	AddInt(BrowserSection, StreamHeightKey, LOCTEXT("StreamHeight", "Stream resolution (lines)"),
		LOCTEXT("StreamHeightTip", "Height of the picture other players get from your browser, window or remote desktop, e.g. 480, 720 or 1080. Taller pictures are scaled down to it; the world host's limit applies. Higher needs more bitrate to look sharp."), 720);
	AddInt(BrowserSection, StreamKbpsKey, LOCTEXT("StreamKbps", "Stream bitrate (kbps)"),
		LOCTEXT("StreamKbpsTip", "Video bitrate of what you share, before it adapts to your connection. About 2500 for 720p, 5000 for 1080p; lower it if your upload is slow. The world host's limit applies."), 2500);
	AddInt(BrowserSection, StreamFpsKey, LOCTEXT("StreamFps", "Stream frame rate"),
		LOCTEXT("StreamFpsTip", "Frames per second of what you share: 30 or 60. The world host's limit applies."), 30);
	AddInt(BrowserSection, StreamEncoderKey, LOCTEXT("StreamEncoder", "Stream encoder"),
		LOCTEXT("StreamEncoderTip", "0: best your graphics card has (AV1, then HEVC, then H.264); 1: prefer AV1; 2: prefer HEVC; 3: prefer H.264; 4: software AV1 (no graphics card encoder, uses the CPU). Every player can watch any of them."), 0);
	AddInt(BrowserSection, BrowserHeightKey, LOCTEXT("BrowserHeight", "Browser page size (lines)"),
		LOCTEXT("BrowserHeightTip", "Height the built-in browser draws pages at (its width follows the screen's shape). Bigger shows more of a page in smaller text. Applies to browsers opened after the change."), 720);
	AddBool(BrowserSection, RemoteMatchScreenKey, LOCTEXT("RemoteMatchScreen", "Ask remote desktops for the screen's shape"),
		LOCTEXT("RemoteMatchScreenTip", "When connecting, ask the VNC server to change its desktop to the screen's shape (at the stream resolution). TigerVNC, x11vnc and QEMU with a virtio display usually follow; others keep their size and show with bars (or use Fill)."), true);
	AddString(BrowserSection, CurtainKeyKey, LOCTEXT("CurtainKey", "Privacy curtain key"),
		LOCTEXT("CurtainKeyTip", "Blanks your shared browser for everyone (you still see it) and back. Key name, e.g. F10."), TEXT("F10"));
	AddBool(BrowserSection, StreamDisclaimerKey, LOCTEXT("StreamDisclaimer", "I understand that shared browsers are visible to everyone"),
		LOCTEXT("StreamDisclaimerTip", "Set when you accept the sharing notice the first time you open the browser. Turn it off to see the notice again."), false);
	AddBool(BrowserSection, WindowAudioKey, LOCTEXT("WindowAudio", "Share a window's sound"),
		LOCTEXT("WindowAudioTip", "Send the shared window's own sound to the screen (needs Windows 11 or Windows 10 21H2 and later). You keep hearing it from the window itself."), true);
	AddBool(BrowserSection, WindowCursorKey, LOCTEXT("WindowCursor", "Show the mouse in a shared window"),
		LOCTEXT("WindowCursorTip", "Include your mouse pointer in the picture of a shared window."), true);
	AddInt(BrowserSection, GrantMinutesKey, LOCTEXT("GrantMinutes", "Control you give lasts (minutes)"),
		LOCTEXT("GrantMinutesTip", "How long a viewer you let use your browser, window or remote desktop keeps that control. F10 or Revoke ends it sooner."), 15);
	AddBool(BrowserSection, GrantDisclaimerKey, LOCTEXT("GrantDisclaimer", "I understand that viewers I give control act on my PC"),
		LOCTEXT("GrantDisclaimerTip", "Set when you accept the notice the first time you give a viewer control. Turn it off to see it again."), false);
	AddBool(BrowserSection, RemoteDisclaimerKey, LOCTEXT("RemoteDisclaimer", "I understand that shared remote desktops are visible to everyone"),
		LOCTEXT("RemoteDisclaimerTip", "Set when you accept the notice the first time you connect a screen to a remote desktop. Turn it off to see it again."), false);
	AddBool(BrowserSection, WindowDisclaimerKey, LOCTEXT("WindowDisclaimer", "I understand that shared windows are visible to everyone"),
		LOCTEXT("WindowDisclaimerTip", "Set when you accept the notice the first time you share a window. Turn it off to see it again."), false);

	UConfigPropertySection* Keys = AddSection(TEXT("Keys"), LOCTEXT("KeysSection", "Keys"));
	AddString(Keys, FocusKeyKey, LOCTEXT("FocusKey", "Window focus key"),
		LOCTEXT("FocusKeyTip", "Switches the screen window between using the browser (mouse and keyboard go to it) and playing (the window stays as a picture). Key name, e.g. F8."), TEXT("F8"));
	AddString(Keys, ScreensListKeyKey, LOCTEXT("ScreensListKey", "Screens list key"),
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
	AddInt(Privacy, ReceiveStreamsKey, LOCTEXT("ReceiveStreams", "Watch other players' browsers and windows"),
		LOCTEXT("ReceiveStreamsTip", "0: show them automatically; 1: never (you see who is using the screen only)."), 0);

	// The world host: the player hosting the world (or a dedicated server's own settings); rules for everyone.
	UConfigPropertySection* Hosting = AddSection(TEXT("Hosting"), LOCTEXT("Hosting", "Rules for this world (world host)"));
	Hosting->Tooltip = LOCTEXT("HostingTip", "Used when this PC hosts the world (on a dedicated server, the server's copy of these settings): what every player may do with screens. Server admins count as the world host.");
	AddInt(Hosting, QueuePolicyKey, LOCTEXT("QueuePolicy", "Who can change screen videos"),
		LOCTEXT("QueuePolicyTip", "0: anyone; 1: only the world host (on a dedicated server: its admins)."), int32(ETSQueuePolicy::Anyone));
	AddString(Hosting, AllowedSitesKey, LOCTEXT("AllowedSites", "Allowed sites"),
		LOCTEXT("AllowedSitesTip", "Comma-separated sites videos can be added from. Also used by the privacy setting above."), DefaultAllowedSites);
	AddBool(Hosting, AllowAnySiteKey, LOCTEXT("AllowAnySite", "Allow videos from any site"),
		LOCTEXT("AllowAnySiteTip", "Let players add links from sites that aren't in the list (each player's privacy setting still applies)."), false);
	AddBool(Hosting, AllowStreamingKey, LOCTEXT("AllowStreaming", "Allow sharing browsers with other players"),
		LOCTEXT("AllowStreamingTip", "Let a player's browser be streamed to everyone through this world's connection. Off: only the player using it sees it."), true);
	AddInt(Hosting, MaxStreamKbpsKey, LOCTEXT("MaxStreamKbps", "Highest stream bitrate (kbps)"),
		LOCTEXT("MaxStreamKbpsTip", "Most each shared browser, window or remote desktop may send through this world's connection, sound included. Sharers' PCs keep below it; the server drops data above it."), 8000);
	AddBool(Hosting, AllowDirectKey, LOCTEXT("AllowDirect", "Allow direct connections between players"),
		LOCTEXT("AllowDirectTip", "Players who turn on direct connections stream to each other directly instead of through this world's connection. The server only passes their addresses and a key between them."), true);
	AddInt(Hosting, MaxStreamHeightKey, LOCTEXT("MaxStreamHeight", "Highest stream resolution (lines)"),
		LOCTEXT("MaxStreamHeightTip", "Sharers' streams are scaled down to at most this height, e.g. 720 or 1080."), 1080);
	AddInt(Hosting, MaxStreamFpsKey, LOCTEXT("MaxStreamFps", "Highest stream frame rate"),
		LOCTEXT("MaxStreamFpsTip", "Most frames per second a sharer may stream."), 60);
	AddBool(Hosting, AllowBrowserKey, LOCTEXT("AllowBrowser", "Allow the built-in browser"),
		LOCTEXT("AllowBrowserTip", "Let players use the built-in browser on screens."), true);
	AddInt(Hosting, BrowserRoleKey, LOCTEXT("BrowserRole", "Who can use the browser"),
		LOCTEXT("BrowserRoleTip", "0: everyone; 1: trusted players (list below) and the world host; 2: only the world host (on a dedicated server: its admins)."),
		int32(ETSRole::Everyone));
	AddInt(Hosting, WindowRoleKey, LOCTEXT("WindowRole", "Who can share a window"),
		LOCTEXT("WindowRoleTip", "Sharing a window shows something from that player's desktop to everyone. 0: everyone; 1: trusted players and the world host; 2: only the world host or server admins."),
		int32(ETSRole::Trusted));
	AddInt(Hosting, RemoteDesktopRoleKey, LOCTEXT("RemoteDesktopRole", "Who can show a remote desktop"),
		LOCTEXT("RemoteDesktopRoleTip", "A remote desktop (VNC) shows a whole computer to everyone. 0: everyone; 1: trusted players and the world host; 2: only the world host or server admins."),
		int32(ETSRole::HostAndAdmins));
	AddInt(Hosting, ViewerInputBrowserKey, LOCTEXT("ViewerInputBrowser", "Most control a viewer can get: browser"),
		LOCTEXT("ViewerInputBrowserTip", "What the player sharing a browser may let a viewer do in it. 0: nothing; 1: mouse; 2: mouse and typing; 3: everything the input filter allows."),
		int32(ETSInputScope::Pointer));
	AddInt(Hosting, ViewerInputWindowKey, LOCTEXT("ViewerInputWindow", "Most control a viewer can get: shared window"),
		LOCTEXT("ViewerInputWindowTip", "Viewers acting in a window on the sharer's PC. 0: nothing; 1: mouse; 2: mouse and typing."), int32(ETSInputScope::None));
	AddInt(Hosting, ViewerInputRemoteKey, LOCTEXT("ViewerInputRemote", "Most control a viewer can get: remote desktop"),
		LOCTEXT("ViewerInputRemoteTip", "Viewers acting on a whole remote computer. 0: nothing; 1: mouse; 2: mouse and typing."), int32(ETSInputScope::None));
	AddString(Hosting, TrustedPlayersKey, LOCTEXT("TrustedPlayers", "Trusted players"),
		LOCTEXT("TrustedPlayersTip", "Comma-separated player names for the \"trusted players\" choices above."), FString());
	AddBool(Hosting, AllowTakeOverKey, LOCTEXT("AllowTakeOver", "Players can take over a screen in use"),
		LOCTEXT("AllowTakeOverTip", "Off: only the player using a screen (or the world host and server admins) can stop it; others wait until it's free."), true);
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
			{UTSConfig::SubtitleModeKey, {0.f, 3.f}},
			{UTSConfig::DirectPortKey, {0.f, 65535.f}},
			{UTSConfig::StreamHeightKey, {240.f, 2160.f}},
			{UTSConfig::StreamEncoderKey, {0.f, 4.f}},
			{UTSConfig::BrowserHeightKey, {360.f, 1440.f}},
			{UTSConfig::MaxStreamHeightKey, {240.f, 2160.f}},
			{UTSConfig::MaxStreamFpsKey, {10.f, 60.f}},
			{UTSConfig::ReceiveStreamsKey, {0.f, 1.f}},
			{UTSConfig::MaxStreamKbpsKey, {500.f, 100000.f}},
			{UTSConfig::PipCornerKey, {0.f, 3.f}},
			{UTSConfig::QueuePolicyKey, {0.f, 1.f}},
			{UTSConfig::BrowserRoleKey, {0.f, 2.f}},
			{UTSConfig::WindowRoleKey, {0.f, 2.f}},
			{UTSConfig::RemoteDesktopRoleKey, {0.f, 2.f}},
			{UTSConfig::ViewerInputBrowserKey, {0.f, 3.f}},
			{UTSConfig::ViewerInputWindowKey, {0.f, 2.f}},
			{UTSConfig::ViewerInputRemoteKey, {0.f, 2.f}},
			{UTSConfig::GrantMinutesKey, {1.f, 120.f}},
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

void UTSConfig::SetInt(const UObject* WorldContext, const FString& Key, int32 Value)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	UConfigPropertyInteger* Property = Manager ? Cast<UConfigPropertyInteger>(FindProperty(WorldContext, Key)) : nullptr;
	if (!Property)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Config: could not set '%s'; configuration not available"), *Key);
		return;
	}
	Property->Value = Value;
	Manager->MarkConfigurationDirty(MakeConfigId());
}

void UTSConfig::SetString(const UObject* WorldContext, const FString& Key, const FString& Value)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	UConfigPropertyString* Property = Manager ? Cast<UConfigPropertyString>(FindProperty(WorldContext, Key)) : nullptr;
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

FString UTSConfig::BrowserAddressFor(const UObject* WorldContext, const FString& Typed)
{
	const FString Text = Typed.TrimStartAndEnd();
	if (Text.IsEmpty() || Text.Contains(TEXT("://")) || Text.StartsWith(TEXT("about:")))
	{
		return Text;
	}
	// One word with a dot and no spaces (youtube.com, 192.168.1.20:8006/x, localhost:8080) is a site.
	const bool bLooksLikeSite = !Text.Contains(TEXT(" ")) && (Text.Contains(TEXT(".")) || Text.StartsWith(TEXT("localhost")));
	if (bLooksLikeSite)
	{
		return TEXT("https://") + Text;
	}
	FString Engine = GetString(WorldContext, SearchEngineKey, TEXT("https://duckduckgo.com/?q=%s")).TrimStartAndEnd();
	if (!Engine.Contains(TEXT("%s")))
	{
		Engine = TEXT("https://duckduckgo.com/?q=%s");
	}
	// The query, URL-encoded byte by byte from UTF-8.
	FString Query;
	const FTCHARToUTF8 Utf8(*Text);
	for (int32 I = 0; I < Utf8.Length(); ++I)
	{
		const uint8 B = uint8(Utf8.Get()[I]);
		const bool bPlain = (B >= '0' && B <= '9') || (B >= 'a' && B <= 'z') || (B >= 'A' && B <= 'Z') || B == '-' || B == '_' || B == '.' || B == '~';
		if (bPlain)
		{
			Query.AppendChar(TCHAR(B));
		}
		else if (B == ' ')
		{
			Query.AppendChar(TEXT('+'));
		}
		else
		{
			Query += FString::Printf(TEXT("%%%02X"), B);
		}
	}
	return Engine.Replace(TEXT("%s"), *Query);
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
