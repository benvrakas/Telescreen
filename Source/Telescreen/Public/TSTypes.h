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
