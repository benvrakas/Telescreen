#include "Net/TSNetRate.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Telescreen.h"

void TSNetRate::Raise(UWorld* World, int32 BytesPerSecond)
{
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver)
	{
		return;
	}
	const bool bChangedDriver = Driver->MaxClientRate < BytesPerSecond || Driver->MaxInternetClientRate < BytesPerSecond;
	Driver->MaxClientRate = FMath::Max(Driver->MaxClientRate, BytesPerSecond);
	Driver->MaxInternetClientRate = FMath::Max(Driver->MaxInternetClientRate, BytesPerSecond);

	int32 Raised = 0;
	auto RaiseConnection = [&](UNetConnection* Connection)
	{
		if (Connection && Connection->CurrentNetSpeed < BytesPerSecond)
		{
			UE_LOG(LogTelescreen, Log, TEXT("Net rate: %s %d -> %d bytes/s"), *Connection->GetName(), Connection->CurrentNetSpeed, BytesPerSecond);
			Connection->CurrentNetSpeed = BytesPerSecond;
			++Raised;
		}
	};
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		RaiseConnection(Connection);
	}
	RaiseConnection(Driver->ServerConnection);
	if (bChangedDriver || Raised > 0)
	{
		UE_LOG(LogTelescreen, Log, TEXT("Net rate raised to %d bytes/s (driver caps %d / %d, %d connection(s) changed)"), BytesPerSecond, Driver->MaxClientRate,
			Driver->MaxInternetClientRate, Raised);
	}
}
