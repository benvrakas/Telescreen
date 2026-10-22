#pragma once

#include "CoreMinimal.h"
#include "TSTypes.generated.h"

// How a picture with a different shape fits the screen.
UENUM(BlueprintType)
enum class ETSAspectMode : uint8
{
	// Whole picture visible, black bars where the shapes differ.
	Letterbox,
	// Screen filled, edges of the picture cut off.
	Fill,
	// Screen filled, picture stretched.
	Stretch,
};

// What a screen shows: the shared synced media queue, or a player's own source streamed to the others.
UENUM(BlueprintType)
enum class ETSScreenMode : uint8
{
	SyncedMedia,
	// The controller's built-in browser.
	Browser,
	// A window on the controller's PC.
	Window,
	// A remote desktop the controller's PC connects to (VNC; RDP later).
	RemoteDesktop,
};

// Lowest role that may use a mode (host setting).
UENUM(BlueprintType)
enum class ETSRole : uint8
{
	Everyone,
	// Players named in the host's trusted list, and the host and admins.
	Trusted,
	// The host (listen server) or server admins (dedicated server).
	HostAndAdmins,
};

// One piece of a live stream packet, small enough to travel as one unreliable network message.
USTRUCT()
struct TELESCREEN_API FTSStreamChunk
{
	GENERATED_BODY()

	// 0 video, 1 audio (the helper's EStreamKind).
	UPROPERTY()
	uint8 Kind = 0;

	// 1: the packet is a keyframe.
	UPROPERTY()
	uint8 Flags = 0;

	// Counts up per kind; a gap means a whole packet was lost.
	UPROPERTY()
	uint32 PacketId = 0;

	UPROPERTY()
	uint16 Index = 0;

	UPROPERTY()
	uint16 Count = 1;

	UPROPERTY()
	uint32 PtsMs = 0;

	// AVCodecID of the stream.
	UPROPERTY()
	uint32 Codec = 0;

	UPROPERTY()
	TArray<uint8> Data;
};

// One entry of a screen's synced media queue.
USTRUCT(BlueprintType)
struct TELESCREEN_API FTSMediaItem
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FString Url;

	UPROPERTY(SaveGame)
	FString Title;

	// Seconds; 0 when unknown or live.
	UPROPERTY(SaveGame)
	float Duration = 0.0f;

	UPROPERTY(SaveGame)
	bool bIsLive = false;

	// Name of the player who added it.
	UPROPERTY(SaveGame)
	FString AddedBy;

	FString GetDisplayTitle() const { return Title.IsEmpty() ? Url : Title; }
};

// A screen's synced media playback, replicated from the server (see Docs/Architecture.md, Synced media).
USTRUCT(BlueprintType)
struct TELESCREEN_API FTSMediaState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TArray<FTSMediaItem> Queue;

	// Index into Queue of the item playing, or INDEX_NONE.
	UPROPERTY(SaveGame)
	int32 CurrentIndex = INDEX_NONE;

	// Server world time at which the current item was at position 0 (not saved; rebuilt from SavedPosition).
	UPROPERTY()
	double StartServerTime = 0.0;

	UPROPERTY(SaveGame)
	bool bPaused = false;

	// Position while paused, and the position written to save games.
	UPROPERTY(SaveGame)
	double PausedPosition = 0.0;

	// Increases with every change so clients notice edits that leave other fields equal.
	UPROPERTY()
	int32 Revision = 0;

	const FTSMediaItem* GetCurrent() const { return Queue.IsValidIndex(CurrentIndex) ? &Queue[CurrentIndex] : nullptr; }

	// Playback position at ServerTime, in seconds.
	double GetPosition(double ServerTime) const { return bPaused ? PausedPosition : FMath::Max(0.0, ServerTime - StartServerTime); }
};
