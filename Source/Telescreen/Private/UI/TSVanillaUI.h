#pragma once

#include "CoreMinimal.h"

class UTextBlock;
class UWidget;
class UWidgetTree;

// Builds UI from the game's own Blueprint widgets (window frame, buttons, tabs, input boxes, scroll box), reached
// through reflection. Each maker falls back to a plain UMG widget, with a log line, if the vanilla class is missing.
// Widget names and members come from the TSDumpWidgets commandlet (see Docs/Architecture.md, UI).
namespace TSVanillaUI
{
	enum class EButtonStyle : uint8
	{
		Normal,
		// Orange: the main action.
		Primary,
	};

	// A vanilla dark window (title, close button) with Body in its content slot; its close button calls
	// Handler.CloseFunction() (a UFUNCTION without parameters).
	UWidget* MakeWindow(UWidgetTree* Tree, const FText& Title, UWidget* Body, UObject* Handler, FName CloseFunction);
	void SetWindowTitle(UWidget* Window, const FText& Title);

	// The window's own header tabs (icon and name each), set before the window is shown. A click calls
	// Handler.ClickFunction(int32 ButtonIndex); Handler.GeneratedFunction() is called once the tab buttons exist.
	// Icons are texture paths (nullptr: none). Returns false if the window has no header tabs.
	bool SetWindowTabs(UWidget* Window, TArrayView<const FText> Names, TArrayView<const TCHAR* const> Icons, UObject* Handler, FName ClickFunction,
		FName GeneratedFunction);
	// Makes header tab Index the active one (as if clicked); false if its button isn't there.
	bool SelectWindowTab(UWidget* Window, int32 Index);
	void SetWindowTabVisible(UWidget* Window, int32 Index, bool bVisible);

	// Body on the vanilla content background, with padding.
	UWidget* MakeContentBackground(UWidgetTree* Tree, UWidget* Body);

	// A vanilla standard button; a click calls Handler.Function() (a UFUNCTION without parameters).
	UWidget* MakeButton(UWidgetTree* Tree, const FText& Text, UObject* Handler, FName Function, EButtonStyle Style = EButtonStyle::Normal);
	void SetButtonText(UWidget* Button, const FText& Text);
	void SetButtonEnabled(UWidget* Button, bool bEnabled);

	// A vanilla tab button; a click calls Handler.Function().
	UWidget* MakeTab(UWidgetTree* Tree, const FText& Text, UObject* Handler, FName Function);
	void SetTabActive(UWidget* Tab, bool bActive);

	// A vanilla input box showing Hint while empty; Enter calls Handler.CommitFunction(const FText&, ETextCommit::Type)
	// when CommitFunction isn't None.
	UWidget* MakeInputBox(UWidgetTree* Tree, const FText& Hint, UObject* Handler = nullptr, FName CommitFunction = NAME_None);
	FText GetInputText(UWidget* InputBox);
	void SetInputText(UWidget* InputBox, const FText& Text);
	bool InputHasFocus(UWidget* InputBox);
	// Shows dots instead of the typed text.
	void SetInputPassword(UWidget* InputBox, bool bPassword);

	// A vanilla list button (title, a grey second line, highlighted while selected); a click calls Handler.Function().
	UWidget* MakeListButton(UWidgetTree* Tree, const FText& Title, const FText& Description, bool bSelected, UObject* Handler, FName Function);

	// A thin horizontal line between sections.
	UWidget* MakeDivider(UWidgetTree* Tree);

	// Content in a vanilla scroll box.
	UWidget* MakeScrollBox(UWidgetTree* Tree, UWidget* Content);

	// A text block in the game's UI font (taken from a vanilla widget), Size points, Bold for headings.
	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false);
}
