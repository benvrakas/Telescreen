#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSStreamIndicator.generated.h"

class ATSBuildableScreen;
class UTextBlock;

// A line at the top of the HUD while the local player shares a browser or window: what, where, how many watch, and
// the curtain key. Shown and updated by UTSClientSubsystem.
UCLASS()
class UTSStreamIndicator : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows or updates the indicator for Screen, or removes it when Screen is null.
	static void Update(const UObject* WorldContext, const ATSBuildableScreen* Screen);

	virtual bool Initialize() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Line;
};
