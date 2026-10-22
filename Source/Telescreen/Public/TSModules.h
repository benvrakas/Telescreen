#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "TSModules.generated.h"

// Telescreen's root game instance module: settings, remote call object, and the vanilla assets the screens reuse.
UCLASS()
class TELESCREEN_API UTSGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UTSGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

// Telescreen's root game world module; registers the AWESOME Shop schematic.
UCLASS()
class TELESCREEN_API UTSGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UTSGameWorldModule();
};
