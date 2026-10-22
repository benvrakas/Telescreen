#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "TSConfig.generated.h"

// Which synced media links set by other players load automatically on this PC.
UENUM()
enum class ETSAutoLoad : uint8
{
	AllowedSitesOnly,
	Everything,
	Never,
};

// Who may change a screen's media queue on a world this player hosts.
UENUM()
enum class ETSQueuePolicy : uint8
{
	Anyone,
	HostOnly,
};

// Telescreen settings, editable in-game (Mods menu) and in Satisfactory Mod Manager. "When hosting" settings apply
// on the machine that runs the world (a dedicated server reads its own config file).
UCLASS()
class TELESCREEN_API UTSConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	UTSConfig();

	static const FString MaxActiveScreensKey;
	static const FString PlayDistanceKey;
	static const FString MaxVideoHeightKey;
	static const FString VolumeKey;
	static const FString AudioRangeKey;
	static const FString MuteInBackgroundKey;
	static const FString AutoLoadKey;
	static const FString QueuePolicyKey;
	static const FString AllowedSitesKey;
	static const FString AllowAnySiteKey;
	static const FString MaxVideoMinutesKey;
	static const FString QueueAddsPerMinuteKey;
	static const FString AllowBrowserKey;
	static const FString BrowserHomeKey;
	static const FString KeepBrowserLoginsKey;
	static const FString FocusKeyKey;
	static const FString ScreensListKeyKey;
	static const FString PipWidthKey;
	static const FString PipCornerKey;
	static const FString PipAudio2DKey;
	static const FString MirrorXKey;
	static const FString MirrorYKey;

	// Rebuilds the default configuration with SML's Blueprint property classes (the ones the Mods menu can edit).
	// Call before SML registers the configuration.
	static void UseSMLEditorClasses();

	static bool GetBool(const UObject* WorldContext, const FString& Key, bool Fallback);
	static float GetFloat(const UObject* WorldContext, const FString& Key, float Fallback);
	static int32 GetInt(const UObject* WorldContext, const FString& Key, int32 Fallback);
	static FString GetString(const UObject* WorldContext, const FString& Key, const FString& Fallback);

	// True if Url's host is one of the "Allowed sites" (or a subdomain of one).
	static bool IsAllowedSite(const UObject* WorldContext, const FString& Url);
};
