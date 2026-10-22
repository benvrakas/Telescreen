#pragma once

#include "CoreMinimal.h"

// Video files on this PC. A queued file is played from its full path when that exists here; otherwise from a file
// with the same name anywhere in this player's videos folder (Mods > Telescreen, or the Videos tab's button), so
// players with their copies in different places (a Plex folder on F: or on C:) still play their own. See
// Docs/Architecture.md, Local videos.
namespace TSLocalVideos
{
	enum class EResolve : uint8
	{
		Found,
		// The videos folder is still being indexed; ask again shortly.
		Searching,
		Missing,
	};

	// Whether a queue entry is a file (not a web link).
	bool IsLocalFile(const FString& Url);

	// The path to play File from on this PC, if it is here.
	EResolve Resolve(const UObject* Context, const FString& File, FString& OutPath);

	// This player's videos folder ("" when not set).
	FString GetFolder(const UObject* Context);
	void SetFolder(const UObject* Context, const FString& Folder);

	// Opens the Windows folder or file picker without stopping the game; OnPicked gets the path ("" when cancelled)
	// on the game thread.
	void PickFolder(const FString& Start, TFunction<void(const FString&)> OnPicked);
	void PickFile(const FString& Start, TFunction<void(const FString&)> OnPicked);

	// Shows the videos folder in Explorer.
	void OpenFolder(const UObject* Context);
}
