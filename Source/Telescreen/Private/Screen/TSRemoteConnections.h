#pragma once

#include "CoreMinimal.h"

// A remote desktop the local player can connect a screen to. Address and password never leave this PC: the screen's
// replicated source is only a token (MakePending), resolved here by the controller's own client.
struct FTSRemoteConnection
{
	FString Host;
	uint16 Port = 5900;
	FString Password;

	// "host:port" as typed (VNC displays below 100 written as host:1 mean port 5901).
	FString GetAddress() const { return FString::Printf(TEXT("%s:%u"), *Host, Port); }
};

namespace TSRemoteConnections
{
	// Reads "host", "host:port" or "host:display" (display < 100 = port 5900 + display). False if the host is empty.
	bool Parse(const FString& Address, const FString& Password, FTSRemoteConnection& Out);

	// Remembers Connection for this game session and returns the token to send as the screen's source ("rd:<id>").
	FString MakePending(const FTSRemoteConnection& Connection);

	// The connection a token stands for, if this PC made it.
	const FTSRemoteConnection* FindPending(const FString& Token);

	// Saved connections in Saved/Telescreen/RemoteConnections.json; passwords encrypted for this Windows user (DPAPI).
	TArray<FTSRemoteConnection> LoadSaved();
	void Save(const TArray<FTSRemoteConnection>& Connections);
}
