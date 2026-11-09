#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "TSScreenWindow.generated.h"

class ATSBuildableScreen;
class UBorder;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UOverlaySlot;
class USizeBox;
class UTextBlock;
class UTexture;
class UTSScreenPlayer;
class UWidget;

class UTSScreenWindow;

// Routes one browser tab button's click (show the tab, or close it) to its window.
UCLASS()
class UTSWindowTabAction : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UTSScreenWindow> Window;

	uint32 Tab = 0;
	bool bClose = false;

	UFUNCTION()
	void HandleClicked();
};

// A screen shown on the HUD. Pass-through: a small picture in a corner that never takes input. Interactive: a large
// window whose mouse and keyboard go to the screen's browser.
UCLASS()
class UTSScreenWindow : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows Screen as the HUD picture, or removes it if it is the one shown.
	static void TogglePinned(ATSBuildableScreen* Screen);

	// Opens Screen in the interactive window.
	static void OpenInteractive(ATSBuildableScreen* Screen);

	// Focus key: switches the open window between interactive and pass-through, or opens the local player's browser.
	static void ToggleFocus(const UObject* WorldContext);

	// True if Screen is shown on the HUD (either state).
	static bool IsShowing(const ATSBuildableScreen* Screen);

	// Removes the window, if any.
	static void Close();

	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent) override;

	// Browser toolbar (the controller's own browser, while interactive).
	UFUNCTION()
	void HandleBack();
	UFUNCTION()
	void HandleForward();
	UFUNCTION()
	void HandleReload();
	UFUNCTION()
	void HandleHome();
	UFUNCTION()
	void HandleAddressCommitted(const FText& Text, ETextCommit::Type Method);
	UFUNCTION()
	void HandleNewTab();
	// A tab button: shows Tab, or closes it.
	void HandleTab(uint32 Tab, bool bClose);

private:
	static UTSScreenWindow* Show(ATSBuildableScreen* Screen);
	void BuildTree();
	void SetInteractive(bool bInInteractive);
	void Place();
	UTSScreenPlayer* GetBrowserPlayer() const;
	bool PicturePosition(const FPointerEvent& Event, FVector2D& OutPosition) const;
	FReply HandleButton(const FPointerEvent& Event, bool bUp, int32 ClickCount);
	void UpdateCursor();
	bool IsFocusKey(const FKey& Key) const;
	// Shows the toolbar for the controller's browser and keeps its buttons and address in step with the page.
	void UpdateToolbar(UTSScreenPlayer* Player);
	// Back, forward or reload from a shortcut (Alt+Left/Right, F5, the mouse's side buttons), or a tab shortcut
	// (Ctrl+T, Ctrl+W, Ctrl+Tab, Ctrl+Shift+Tab); false if Key isn't one.
	bool HandleNavigationKey(const FKey& Key, bool bAlt, bool bCtrl, bool bShift);
	// Rebuilds the tab strip when the browser's tabs changed.
	void UpdateTabs(UTSScreenPlayer* Own);
	UTSScreenPlayer* GetOwnBrowser() const;

	TWeakObjectPtr<ATSBuildableScreen> Screen;
	bool bInteractive = false;
	bool bPinned = false;
	bool bHighPrecision = false;
	double EscapeDownAt = 0.0;
	// When the window last became interactive, and whether input could reach the source since then.
	double InteractiveSince = 0.0;
	bool bInteractionSeen = false;

	// The widget covers the whole viewport; Frame (inside Size) is aligned in it, so placement doesn't depend on
	// viewport-slot sizing.
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Backdrop;
	UPROPERTY(Transient)
	TObjectPtr<UOverlaySlot> FrameSlot;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Size;
	UPROPERTY(Transient)
	TObjectPtr<UImage> Picture;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;
	// Names next to viewer cursors and pings.
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> MarkerLayer;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> MarkerLabels;
	void UpdateMarkerLabels(const class UTSScreenPlayer* Player);
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Hint;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> ShownTexture;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> Toolbar;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> BackButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> ForwardButton;
	UPROPERTY(Transient)
	TObjectPtr<UWidget> AddressBox;
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> TabStrip;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSWindowTabAction>> TabActions;
	// The tab list the strip shows (ids, titles, the shown one), to rebuild it only when that changes.
	FString ShownTabsKey;
	bool bToolbarShown = false;
	FString ShownAddress;

	FVector2D PlacedSize = FVector2D::ZeroVector;
	FVector2D PlacedAnchor = FVector2D(-1.f, -1.f);
	bool bPlacedInteractive = false;
};
