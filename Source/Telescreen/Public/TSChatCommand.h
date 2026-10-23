#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "TSChatCommand.generated.h"

class ATSBuildableScreen;

// "/ts list" lists every screen in the world, numbered: what it shows, who uses it, how many watch, how far away.
// "/ts stop <number|all>" ends the screen's sharing (browser, window, remote desktop) and stops its video (the queue
// stays); the world host, server admins and the server console may stop any screen, other players only the one they
// share. Runs on the server, which knows every screen.
UCLASS()
class TELESCREEN_API ATSChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ATSChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

private:
	EExecutionStatus List(UCommandSender* Sender);
	EExecutionStatus Stop(UCommandSender* Sender, const FString& Which);
	// Every screen in a stable order (by name, then position), as numbered by /ts list.
	TArray<ATSBuildableScreen*> GetScreens() const;
};
