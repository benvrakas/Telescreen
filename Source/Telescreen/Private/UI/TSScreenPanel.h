#pragma once

#include "CoreMinimal.h"
#include "TSTypes.h"
#include "UI/FGInteractWidget.h"
#include "TSScreenPanel.generated.h"

class ATSBuildableScreen;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UTSScreenPanel;

UENUM()
enum class ETSPanelAction : uint8
{
	Close,
	PlayNow,
	AddToQueue,
	PlayPause,
	Next,
	Stop,
	Back10,
	Forward10,
	PlayIndex,
	RemoveIndex,
	ClearQueue,
	SaveName,
	AspectLetterbox,
	AspectFill,
	AspectStretch,
	LoadAnyway,
};

// Routes one button's click to its panel with the button's action and queue index.
UCLASS()
class UTSPanelAction : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UTSScreenPanel> Panel;

	ETSPanelAction Action = ETSPanelAction::Close;
	int32 Index = INDEX_NONE;

	UFUNCTION()
	void HandleClicked();
};

// The window that opens when a player uses a Telescreen: now playing, playback controls, the shared queue, screen settings.
UCLASS()
class UTSScreenPanel : public UFGInteractWidget
{
	GENERATED_BODY()

public:
	UTSScreenPanel(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void OnEscapePressed_Implementation() override;

	void HandleAction(ETSPanelAction Action, int32 Index);

private:
	void BuildTree();
	ATSBuildableScreen* GetScreen() const;
	void Refresh();
	void RefreshTimes();
	void RebuildQueue();
	void SetNotice(const FText& Text, bool bError = false);
	void AddLink(bool bPlayNow);
	void Close();

	UWidget* MakeButton(const FText& Label, ETSPanelAction Action, int32 Index = INDEX_NONE, const FLinearColor& Color = FLinearColor(0.12f, 0.13f, 0.16f));
	UTextBlock* MakeText(const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor::White);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSPanelAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NowPlayingText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoticeText;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> LinkBox;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> QueueBox;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LoadAnywayButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AspectText;

	FDelegateHandle ScreenChangedHandle;
	FDelegateHandle NoticeHandle;
	int32 ShownRevision = -1;
	float TimeRefresh = 0.f;
};
