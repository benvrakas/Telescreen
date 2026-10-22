#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "TSDumpWidgetsCommandlet.generated.h"

// Development tool: writes the reflection data of vanilla widget classes (properties, functions with parameters, event
// dispatchers, widget tree with named slots) to a text file, so the panel can be built from the game's own widgets.
//   UnrealEditor-Cmd FactoryGame.uproject -run=TSDumpWidgets -out=<file> -classes=<path_C>,<path_C>,...
UCLASS()
class UTSDumpWidgetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
