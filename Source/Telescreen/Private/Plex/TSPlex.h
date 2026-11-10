#pragma once

#include "CoreMinimal.h"
#include "TSTypes.h"

class ATSBuildableScreen;

// Plex, on game clients (Docs/Architecture.md, Plex): this player's Plex sign-in (a link code at plex.tv/link; the
// account token is kept encrypted for the Windows user), their servers, browsing a server's movies and shows, sharing
// a server on a screen with a temporary token, and turning a plex:// queue entry into a stream URL on this PC.
// Callbacks run on the game thread.
namespace TSPlex
{
	struct FServer
	{
		FString Name;
		FString MachineId;
		bool bOwned = false;
		// Base URLs to browse from this PC: local, then remote, then Plex's relay.
		TArray<FString> Connections;
		// The order for other players: remote, then local, then the relay.
		TArray<FString> ShareConnections;
		// This account's token for the server.
		FString AccessToken;
	};

	struct FEntry
	{
		FString Title;
		// Second line: year, length, show and episode.
		FString Details;
		// movie, show, season, episode or section.
		FString Type;
		// Path to browse into (folders), e.g. /library/metadata/123/children.
		FString Key;
		FString RatingKey;
		float Duration = 0.f;
		// Title for the queue, e.g. "Show S01E02 - Name".
		FString QueueTitle;

		bool IsPlayable() const { return Type == TEXT("movie") || Type == TEXT("episode") || Type == TEXT("clip"); }
	};

	// plex://<machine id>/<rating key>
	bool IsPlexUrl(const FString& Url);
	FString MakeUrl(const FString& MachineId, const FString& RatingKey);
	bool ParseUrl(const FString& Url, FString& OutMachineId, FString& OutRatingKey);

	// Account (this PC).
	bool IsSignedIn();
	FString GetUserName();
	// Gets a link code (OnCode) for the player to enter at plex.tv/link, then waits for them (OnDone).
	void SignIn(TFunction<void(const FString& Code)> OnCode, TFunction<void(bool bOk, const FString& Error)> OnDone);
	void CancelSignIn();
	bool IsSigningIn();
	void SignOut();

	// The account's servers (cached after the first call unless bRefresh).
	void ListServers(bool bRefresh, TFunction<void(bool bOk, const TArray<FServer>& Servers, const FString& Error)> OnDone);
	const FServer* FindServer(const FString& MachineId);
	// The server picked in the Plex tab last time, if any.
	FString GetLastServer();
	void SetLastServer(const FString& MachineId);

	// Path "" lists the video libraries; otherwise a library or folder key (FEntry::Key).
	void Browse(const FServer& Server, const FString& Path, TFunction<void(bool bOk, const TArray<FEntry>& Entries, const FString& Error)> OnDone);
	void Search(const FServer& Server, const FString& Query, TFunction<void(bool bOk, const TArray<FEntry>& Entries, const FString& Error)> OnDone);

	// A share for the screen: the server's addresses and a temporary token that only works on that server.
	void MakeShare(const FServer& Server, TFunction<void(bool bOk, const FTSPlexShare& Share, const FString& Error)> OnDone);

	// Shares a server on Screen when this player can and it isn't shared there with a working token yet; at most
	// every half minute per server.
	void EnsureShared(ATSBuildableScreen* Screen, const FString& MachineId);

	enum class EResolve : uint8
	{
		Found,
		// Looking it up; ask again (GetGeneration changes when an answer comes).
		Searching,
		Missing,
	};

	// The URL to play a plex:// entry from on this PC, using the screen's share.
	EResolve Resolve(const ATSBuildableScreen* Screen, const FString& Url, FString& OutPlayUrl, FString& OutProblem);

	// Changes whenever a lookup finishes.
	uint32 GetGeneration();
}
