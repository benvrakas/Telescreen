#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSScreenWindow.generated.h"

class ATSBuildableScreen;
class UBorder;
class UImage;
class UOverlaySlot;
class USizeBox;
class UTextBlock;
class UTexture;
class UTSScreenPlayer;

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

	TWeakObjectPtr<ATSBuildableScreen> Screen;
	bool bInteractive = false;
	bool bPinned = false;
	bool bHighPrecision = false;
	double EscapeDownAt = 0.0;

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
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Hint;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> ShownTexture;

	FVector2D PlacedSize = FVector2D::ZeroVector;
	FVector2D PlacedAnchor = FVector2D(-1.f, -1.f);
	bool bPlacedInteractive = false;
};
