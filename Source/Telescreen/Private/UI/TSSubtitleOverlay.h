#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSSubtitleOverlay.generated.h"

class UTextBlock;
class UWidget;

// Subtitles at the bottom of the HUD for the nearest screen the local player hears (or the one shown on the HUD), when
// the subtitle setting includes the HUD. Shown and updated by UTSClientSubsystem.
UCLASS()
class UTSSubtitleOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows Text, or removes the overlay when Text is empty.
	static void Update(const UObject* WorldContext, const FString& Text);

	virtual bool Initialize() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Line;
};
