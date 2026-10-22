#pragma once

#include "CoreMinimal.h"
#include "Host/TSHostSubsystem.h"
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
	TogglePip,
	BrowserOpen,
	BrowserGo,
	BrowserBack,
	BrowserForward,
	BrowserReload,
	BrowserWindow,
	BrowserStop,
	BrowserTakeOver,
	LoopbackToggle,
	CurtainToggle,
	HostStop,
	WindowList,
	WindowShare,
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
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PipButtonText;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> BrowserUrlBox;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserStartRow;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserOwnRow;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserOtherRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BrowserOtherText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BrowserNote;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserUrlRow;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserOwnActions;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LoopbackRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LoopbackText;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> StreamRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StreamText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurtainButtonText;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> HostStopButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> TakeOverButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StopButtonText;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> WindowSection;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> WindowListBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WindowNote;

	// The windows last listed by "Choose a window" (this PC only).
	TArray<FTSWindowInfo> Windows;
	void ShowWindowList();

	// True while a sharing notice is shown and the next press of the same button accepts it.
	bool bDisclaimerPending = false;
	bool bWindowDisclaimerPending = false;
	bool AcceptSharingNotice();
	bool AcceptWindowNotice();

	void RefreshBrowser();
	FDelegateHandle BrowserStateHandle;

	FDelegateHandle ScreenChangedHandle;
	FDelegateHandle NoticeHandle;
	int32 ShownRevision = -1;
	float TimeRefresh = 0.f;
};
