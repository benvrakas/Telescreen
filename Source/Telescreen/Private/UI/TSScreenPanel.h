#pragma once

#include "CoreMinimal.h"
#include "Host/TSHostSubsystem.h"
#include "Screen/TSRemoteConnections.h"
#include "TSTypes.h"
#include "UI/FGInteractWidget.h"
#include "TSScreenPanel.generated.h"

class ATSBuildableScreen;
class UEditableTextBox;
class UImage;
class UTexture;
class USizeBox;
class UWrapBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
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
	OpenWindow,
	StopSharing,
	BrowserTakeOver,
	LoopbackToggle,
	CurtainToggle,
	HostStop,
	WindowRefresh,
	WindowShare,
	RemoteConnect,
	RemoteSave,
	RemoteSavedConnect,
	RemoteSavedRemove,
	GrantPointer,
	GrantTyping,
	GrantFull,
	GrantRevoke,
	Tab,
	ConfirmAccept,
	ConfirmCancel,
	PickFile,
	SubtitleMode,
	SubtitleTrack,
	ChangeVideosFolder,
	OpenVideosFolder,
};

// Routes one button's click to its panel with the button's action and index (queue row, window row or tab).
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

	// Enter in a text field runs the field's action.
	UFUNCTION()
	void HandleCommitted(const FText& Text, ETextCommit::Type Method);
};

// The window that opens when a player uses a Telescreen, sized to the viewport. The left column shows the screen's
// picture, what it shows, my volume and the screen's volume, Screen settings and, while someone shares, the sharing
// card (controls, viewers); mode tabs pick the right column's page: Videos (playback, adding links, the queue),
// Browser, Share a window, Remote desktop; Screen settings shows the name and picture fit there. First-time sharing
// notices are a confirm dialog over the panel.
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

	// The vanilla window frame's close button.
	UFUNCTION()
	void OnWindowClosed();

	// Screen volume (everyone): letting go sends it to the server.
	UFUNCTION()
	void OnScreenVolumeBegin();
	UFUNCTION()
	void OnScreenVolumeChanged(float Value);
	UFUNCTION()
	void OnScreenVolumeEnd();

	// Seek bar: dragging previews the time; letting go seeks everyone there.
	UFUNCTION()
	void OnSeekBegin();
	UFUNCTION()
	void OnSeekChanged(float Value);
	UFUNCTION()
	void OnSeekEnd();

	// My volume: changes the sound at once; letting go saves it.
	UFUNCTION()
	void OnVolumeChanged(float Value);
	UFUNCTION()
	void OnVolumeEnd();

private:
	enum class ETab : uint8
	{
		Videos,
		Browser,
		Window,
		RemoteDesktop,
		Screen,
	};

	enum class EConfirm : uint8
	{
		None,
		OpenBrowser,
		TakeOver,
		ShareWindow,
		ConnectRemote,
		Grant,
	};

	void BuildTree();
	ATSBuildableScreen* GetScreen() const;
	void Refresh();
	void RefreshTimes();
	void RefreshSharing();
	void RebuildQueue();
	void SetNotice(const FText& Text, bool bError = false);
	void AddLink(bool bPlayNow);
	void Close();
	void SelectTab(ETab Tab);
	void FitToViewport();
	void RequestWindowList();
	void ShowWindowList();
	void AskConfirm(EConfirm Kind, int32 Index);
	void RunConfirmed(EConfirm Kind, int32 Index);

	UTSPanelAction* MakeHandler(ETSPanelAction Action, int32 Index);
	UWidget* MakeButton(const FText& Label, ETSPanelAction Action, int32 Index = INDEX_NONE, bool bPrimary = false);
	UWidget* MakeListButton(const FText& Title, const FText& Description, bool bSelected, ETSPanelAction Action, int32 Index);
	UWidget* MakeInput(const FText& Hint, ETSPanelAction OnEnter);
	UTextBlock* MakeText(const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bBold = false);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSPanelAction>> Actions;

	static constexpr float LeftColumnWidth = 380.f;

	// Frame and left column.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> Window;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelSize;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PagesSize;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PreviewSize;
	UPROPERTY(Transient)
	TObjectPtr<UImage> PreviewImage;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> ShownPreview;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> PipButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoticeText;

	// Sharing card (left column): the controller's controls, or what a viewer can do.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> SharingBar;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SharingText;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> OpenWindowButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> CurtainButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> StopSharingButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> TakeOverButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> HostStopButton;
	// The controller's viewers and what each may do.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> ViewersHeader;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ViewersBox;
	FString ShownViewers;
	void RebuildViewers();
	ETSInputScope PendingGrantScope = ETSInputScope::None;

	// The mode tabs (Videos, Browser, Share a window, Remote desktop); Screen settings is a button in the left column.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> TabButtons;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> SettingsButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Pages;
	ETab CurrentTab = ETab::Videos;

	// Videos page.
	UPROPERTY(Transient)
	TObjectPtr<USlider> SeekSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SeekPositionText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SeekDurationText;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> SeekRow;
	bool bSeeking = false;

	// My volume (left column).
	UPROPERTY(Transient)
	TObjectPtr<USlider> VolumeSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VolumeText;
	UPROPERTY(Transient)
	TObjectPtr<USlider> ScreenVolumeSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ScreenVolumeText;
	bool bScreenVolumeDragging = false;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LinkBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VideosFolderText;

	// Subtitles (Videos page): where they show, and the playing video's tracks.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> SubtitleModeButtons;
	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> SubtitleTracksBox;
	FString ShownSubtitles;
	FDelegateHandle SubtitlesHandle;
	void RebuildSubtitles();
	void RefreshVideosFolder();
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LoadAnywayButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> QueueBox;

	// Browser page.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserUrlBox;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserOpenButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BrowserNavRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BrowserNote;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> LoopbackRow;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LoopbackText;

	// Window page.
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WindowNote;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> WindowListBox;
	TArray<FTSWindowInfo> Windows;
	bool bWindowListPending = false;

	// Remote desktop page.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> RemoteAddressBox;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> RemotePasswordBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RemoteNote;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RemoteListBox;
	TArray<FTSRemoteConnection> SavedRemotes;
	bool bRemoteListLoaded = false;
	void ShowSavedRemotes();
	// Starts showing Connection on this screen (after the first-use notice).
	void ConnectRemote(const struct FTSRemoteConnection& Connection);
	int32 PendingRemoteIndex = INDEX_NONE;

	// Screen page.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> NameBox;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AspectText;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> AspectButtons;

	// Confirm dialog.
	UPROPERTY(Transient)
	TObjectPtr<UWidget> ConfirmLayer;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmTitle;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmText;
	EConfirm PendingConfirm = EConfirm::None;
	int32 PendingIndex = INDEX_NONE;

	// After the local player opens a browser, the interactive window opens once the server hands over control.
	double OpenWindowWhenMineUntil = 0.0;

	FDelegateHandle BrowserStateHandle;
	FDelegateHandle ScreenChangedHandle;
	FDelegateHandle NoticeHandle;
	int32 ShownRevision = -1;
	FText ShownTitle;
	float TimeRefresh = 0.f;
};
