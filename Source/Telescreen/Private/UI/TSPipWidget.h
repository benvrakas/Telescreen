#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSPipWidget.generated.h"

class ATSBuildableScreen;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture;

// Picture-in-picture of one screen in a corner of the HUD; never takes mouse or keyboard input.
UCLASS()
class UTSPipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Pins Screen to the local player's HUD, or unpins it if it is the pinned one.
	static void Toggle(ATSBuildableScreen* Screen);

	// True if Screen is the screen shown on the HUD.
	static bool IsPinned(const ATSBuildableScreen* Screen);

	// Removes the picture-in-picture, if any.
	static void Unpin();

	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();
	void Place();

	TWeakObjectPtr<ATSBuildableScreen> Screen;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Size;
	UPROPERTY(Transient)
	TObjectPtr<UImage> Picture;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> ShownTexture;

	float PlacedWidth = 0.f;
	float PlacedAspect = 0.f;
	int32 PlacedCorner = -1;
};
