#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "TSScreensList.generated.h"

class ATSBuildableScreen;
class UTextBlock;
class UVerticalBox;
class UTSScreensList;

// Routes one row button of the screens list.
UCLASS()
class UTSScreensListAction : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UTSScreensList> List;

	TWeakObjectPtr<ATSBuildableScreen> Screen;
	uint8 Action = 0;

	UFUNCTION()
	void HandleClicked();
};

// Every screen in the world, with its name, what it shows, and buttons to show it on the HUD or open its window.
UCLASS()
class UTSScreensList : public UUserWidget
{
	GENERATED_BODY()

public:
	// Opens the list, or closes it if it is open.
	static void Toggle(const UObject* WorldContext);

	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void HandleAction(ATSBuildableScreen* Screen, uint8 Action);
	void CloseList();

private:
	void BuildTree();
	void Rebuild();
	UWidget* MakeButton(const FText& Label, ATSBuildableScreen* Screen, uint8 Action);

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Rows;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSScreensListAction>> Actions;

	float RebuildIn = 0.f;
};

// Client-side hotkeys: the window focus key and the screens list key (both set in the mod's settings).
UCLASS()
class UTSClientSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
};
