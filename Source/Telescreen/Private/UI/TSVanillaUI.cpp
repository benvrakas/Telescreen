#include "UI/TSVanillaUI.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Telescreen.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* WindowClass = TEXT("/Game/FactoryGame/Interface/UI/Widget_Window_DarkMode.Widget_Window_DarkMode_C");
	const TCHAR* ContentBackgroundClass = TEXT("/Game/FactoryGame/Interface/UI/InGame/Widget_Window_ContentBackground_DarkMode.Widget_Window_ContentBackground_DarkMode_C");
	const TCHAR* StandardButtonClass = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/Widget_StandardButton.Widget_StandardButton_C");
	// Only probed for its bold title font.
	const TCHAR* TabButtonClass = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/Widget_SlidingTabs_Button.Widget_SlidingTabs_Button_C");
	const TCHAR* InputBoxClass = TEXT("/Game/FactoryGame/Interface/UI/Menu/Widget_InputBox.Widget_InputBox_C");
	const TCHAR* ScrollBoxClass = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/Widget_Scrollbox.Widget_Scrollbox_C");

	UClass* LoadWidgetClass(const TCHAR* Path)
	{
		static TMap<FString, TWeakObjectPtr<UClass>> Cache;
		if (const TWeakObjectPtr<UClass>* Found = Cache.Find(Path); Found && Found->IsValid())
		{
			return Found->Get();
		}
		UClass* Class = LoadClass<UUserWidget>(nullptr, Path);
		if (!Class)
		{
			UE_LOG(LogTelescreen, Warning, TEXT("UI: vanilla widget %s not found; using a plain widget"), Path);
		}
		Cache.Add(Path, Class);
		return Class;
	}

	UUserWidget* MakeVanilla(UWidgetTree* Tree, const TCHAR* Path)
	{
		UClass* Class = Tree ? LoadWidgetClass(Path) : nullptr;
		return Class ? Tree->ConstructWidget<UUserWidget>(Class) : nullptr;
	}

	// Calls a Blueprint function; Fill sets its parameters. Returns false (logged once per name) if it doesn't exist.
	bool Call(UObject* Object, FName Name, TFunctionRef<void(UFunction*, uint8*)> Fill)
	{
		UFunction* Function = Object ? Object->FindFunction(Name) : nullptr;
		if (!Function)
		{
			static TSet<FName> Reported;
			if (Object && !Reported.Contains(Name))
			{
				Reported.Add(Name);
				UE_LOG(LogTelescreen, Warning, TEXT("UI: %s has no function %s"), *Object->GetClass()->GetName(), *Name.ToString());
			}
			return false;
		}
		uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, FMath::Max<int32>(Function->ParmsSize, 1));
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		Fill(Function, Params);
		Object->ProcessEvent(Function, Params);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
		return true;
	}

	bool CallText(UObject* Object, FName Name, const FText& Text)
	{
		return Call(Object, Name, [&](UFunction* Function, uint8* Params)
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (FTextProperty* AsText = CastField<FTextProperty>(*It))
				{
					AsText->SetPropertyValue_InContainer(Params, Text);
					return;
				}
			}
		});
	}

	bool CallBools(UObject* Object, FName Name, std::initializer_list<bool> Values)
	{
		return Call(Object, Name, [&](UFunction* Function, uint8* Params)
		{
			const bool* Next = Values.begin();
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm) && Next != Values.end(); ++It)
			{
				if (FBoolProperty* AsBool = CastField<FBoolProperty>(*It))
				{
					AsBool->SetPropertyValue_InContainer(Params, *Next++);
				}
			}
		});
	}

	void SetBoolProperty(UObject* Object, FName Name, bool Value)
	{
		if (FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr)
		{
			Property->SetPropertyValue_InContainer(Object, Value);
		}
	}

	void SetTextProperty(UObject* Object, FName Name, const FText& Value)
	{
		if (FTextProperty* Property = Object ? FindFProperty<FTextProperty>(Object->GetClass(), Name) : nullptr)
		{
			Property->SetPropertyValue_InContainer(Object, Value);
		}
	}

	UObject* GetObjectProperty(const UObject* Object, FName Name)
	{
		FObjectPropertyBase* Property = Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
		return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
	}

	void BindClick(UButton* Button, UObject* Handler, FName Function)
	{
		if (Button && Handler && !Function.IsNone())
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(Handler, Function);
			Button->OnClicked.Add(Delegate);
		}
	}

	UButton* InnerButton(UWidget* Widget)
	{
		return Cast<UButton>(GetObjectProperty(Widget, TEXT("mButton")));
	}

	UEditableText* InnerEditable(UWidget* Widget)
	{
		return Cast<UEditableText>(GetObjectProperty(Widget, TEXT("mInputBox")));
	}

	// The game's UI font, read once from a vanilla widget's text (regular from the input box, bold from a tab title).
	const FSlateFontInfo* GameFont(UWidgetTree* Tree, bool bBold)
	{
		static TOptional<FSlateFontInfo> Regular, Bold;
		static bool bProbed = false;
		if (!bProbed && Tree)
		{
			bProbed = true;
			auto Probe = [Tree](const TCHAR* Path, FName TextName, TOptional<FSlateFontInfo>& Out)
			{
				UUserWidget* Widget = MakeVanilla(Tree, Path);
				const UTextBlock* Text = Widget && Widget->WidgetTree ? Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TextName)) : nullptr;
				if (Text && Text->GetFont().FontObject)
				{
					Out = Text->GetFont();
					const_cast<UObject*>(Out->FontObject.Get())->AddToRoot();
					UE_LOG(LogTelescreen, Log, TEXT("UI: game font %s (%s) from %s"), *GetNameSafe(Out->FontObject), *Out->TypefaceFontName.ToString(), Path);
				}
			};
			Probe(InputBoxClass, TEXT("InputText"), Regular);
			Probe(TabButtonClass, TEXT("mTitleObject"), Bold);
		}
		const TOptional<FSlateFontInfo>& Font = bBold && Bold.IsSet() ? Bold : Regular;
		return Font.IsSet() ? &Font.GetValue() : nullptr;
	}
}

UWidget* TSVanillaUI::MakeWindow(UWidgetTree* Tree, const FText& Title, UWidget* Body, UObject* Handler, FName CloseFunction)
{
	if (UUserWidget* Window = MakeVanilla(Tree, WindowClass))
	{
		SetTextProperty(Window, TEXT("mTitleText"), Title);
		SetBoolProperty(Window, TEXT("mShowCloseButton"), true);
		CallText(Window, TEXT("SetTitle"), Title);
		Window->SetContentForSlot(TEXT("WindowBody"), Body);
		if (Handler && !CloseFunction.IsNone())
		{
			if (FMulticastDelegateProperty* OnClose = FindFProperty<FMulticastDelegateProperty>(Window->GetClass(), TEXT("OnClose")))
			{
				FScriptDelegate Delegate;
				Delegate.BindUFunction(Handler, CloseFunction);
				OnClose->AddDelegate(Delegate, Window);
			}
		}
		return Window;
	}
	UBorder* Frame = Tree->ConstructWidget<UBorder>();
	Frame->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.045f, 0.97f));
	Frame->SetPadding(FMargin(16.f));
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(MakeText(Tree, Title, 22, FLinearColor::White, true));
	UVerticalBoxSlot* BodySlot = Column->AddChildToVerticalBox(Body);
	BodySlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	Frame->AddChild(Column);
	return Frame;
}

void TSVanillaUI::SetWindowTitle(UWidget* Window, const FText& Title)
{
	if (UUserWidget* AsUser = Cast<UUserWidget>(Window))
	{
		SetTextProperty(AsUser, TEXT("mTitleText"), Title);
		CallText(AsUser, TEXT("SetTitle"), Title);
	}
}

UWidget* TSVanillaUI::MakeContentBackground(UWidgetTree* Tree, UWidget* Body)
{
	UOverlay* Layers = Tree->ConstructWidget<UOverlay>();
	if (UUserWidget* Background = MakeVanilla(Tree, ContentBackgroundClass))
	{
		UOverlaySlot* BackSlot = Layers->AddChildToOverlay(Background);
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
		BackSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UOverlaySlot* BodySlot = Layers->AddChildToOverlay(Body);
	BodySlot->SetHorizontalAlignment(HAlign_Fill);
	BodySlot->SetVerticalAlignment(VAlign_Fill);
	BodySlot->SetPadding(FMargin(14.f, 12.f));
	return Layers;
}

UWidget* TSVanillaUI::MakeButton(UWidgetTree* Tree, const FText& Text, UObject* Handler, FName Function, EButtonStyle Style)
{
	const bool bOrange = Style == EButtonStyle::Primary;
	if (UUserWidget* Button = MakeVanilla(Tree, StandardButtonClass))
	{
		SetTextProperty(Button, TEXT("mText"), Text);
		SetBoolProperty(Button, TEXT("mIsOrangeButton"), bOrange);
		SetBoolProperty(Button, TEXT("mIsCenterAligned"), true);
		SetBoolProperty(Button, TEXT("mHideIcon"), true);
		CallText(Button, TEXT("SetText"), Text);
		CallBools(Button, TEXT("SetButtonStyle"), {bOrange, false, false});
		BindClick(InnerButton(Button), Handler, Function);
		return Button;
	}
	UButton* Plain = Tree->ConstructWidget<UButton>();
	Plain->SetBackgroundColor(bOrange ? FLinearColor(0.6f, 0.32f, 0.05f) : FLinearColor(0.12f, 0.13f, 0.16f));
	Plain->AddChild(MakeText(Tree, Text, 13, FLinearColor::White));
	BindClick(Plain, Handler, Function);
	return Plain;
}

void TSVanillaUI::SetButtonText(UWidget* Button, const FText& Text)
{
	if (UButton* Plain = Cast<UButton>(Button))
	{
		if (UTextBlock* Label = Cast<UTextBlock>(Plain->GetChildAt(0)))
		{
			Label->SetText(Text);
		}
		return;
	}
	if (UUserWidget* AsUser = Cast<UUserWidget>(Button))
	{
		SetTextProperty(AsUser, TEXT("mText"), Text);
		CallText(AsUser, TEXT("SetText"), Text);
	}
}

void TSVanillaUI::SetButtonEnabled(UWidget* Button, bool bEnabled)
{
	if (UUserWidget* AsUser = Cast<UUserWidget>(Button); AsUser && AsUser->FindFunction(TEXT("SetIsDisabled")))
	{
		CallBools(AsUser, TEXT("SetIsDisabled"), {!bEnabled});
	}
	else if (Button)
	{
		Button->SetIsEnabled(bEnabled);
	}
}

UWidget* TSVanillaUI::MakeTab(UWidgetTree* Tree, const FText& Text, UObject* Handler, FName Function)
{
	// The vanilla sliding-tab button takes its label and width from a tab bar container, so standalone tabs are
	// standard buttons, orange while selected.
	return MakeButton(Tree, Text, Handler, Function);
}

void TSVanillaUI::SetTabActive(UWidget* Tab, bool bActive)
{
	if (UButton* Plain = Cast<UButton>(Tab))
	{
		Plain->SetBackgroundColor(bActive ? FLinearColor(0.6f, 0.32f, 0.05f) : FLinearColor(0.12f, 0.13f, 0.16f));
		return;
	}
	if (UUserWidget* AsUser = Cast<UUserWidget>(Tab))
	{
		SetBoolProperty(AsUser, TEXT("mIsOrangeButton"), bActive);
		CallBools(AsUser, TEXT("SetButtonStyle"), {bActive, false, false});
	}
}

UWidget* TSVanillaUI::MakeInputBox(UWidgetTree* Tree, const FText& Hint, UObject* Handler, FName CommitFunction)
{
	if (UUserWidget* Box = MakeVanilla(Tree, InputBoxClass))
	{
		SetTextProperty(Box, TEXT("mPreviewText"), Hint);
		// A text field, not the search bar style (no magnifier or clear button).
		SetBoolProperty(Box, TEXT("isSearchbar"), false);
		CallText(Box, TEXT("SetPreviewText"), Hint);
		if (UEditableText* Inner = InnerEditable(Box))
		{
			if (Handler && !CommitFunction.IsNone())
			{
				FScriptDelegate Delegate;
				Delegate.BindUFunction(Handler, CommitFunction);
				Inner->OnTextCommitted.Add(Delegate);
			}
			return Box;
		}
		UE_LOG(LogTelescreen, Warning, TEXT("UI: the vanilla input box has no mInputBox; using a plain text box"));
	}
	UEditableTextBox* Plain = Tree->ConstructWidget<UEditableTextBox>();
	Plain->SetHintText(Hint);
	if (Handler && !CommitFunction.IsNone())
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(Handler, CommitFunction);
		Plain->OnTextCommitted.Add(Delegate);
	}
	return Plain;
}

FText TSVanillaUI::GetInputText(UWidget* InputBox)
{
	if (const UEditableTextBox* Plain = Cast<UEditableTextBox>(InputBox))
	{
		return Plain->GetText();
	}
	const UEditableText* Inner = InnerEditable(InputBox);
	return Inner ? Inner->GetText() : FText::GetEmpty();
}

void TSVanillaUI::SetInputText(UWidget* InputBox, const FText& Text)
{
	if (UEditableTextBox* Plain = Cast<UEditableTextBox>(InputBox))
	{
		Plain->SetText(Text);
	}
	else if (UEditableText* Inner = InnerEditable(InputBox))
	{
		Inner->SetText(Text);
	}
}

bool TSVanillaUI::InputHasFocus(UWidget* InputBox)
{
	if (const UEditableTextBox* Plain = Cast<UEditableTextBox>(InputBox))
	{
		return Plain->HasKeyboardFocus();
	}
	const UEditableText* Inner = InnerEditable(InputBox);
	return Inner && Inner->HasKeyboardFocus();
}

void TSVanillaUI::SetInputPassword(UWidget* InputBox, bool bPassword)
{
	if (UEditableTextBox* Plain = Cast<UEditableTextBox>(InputBox))
	{
		Plain->SetIsPassword(bPassword);
	}
	else if (UEditableText* Inner = InnerEditable(InputBox))
	{
		Inner->SetIsPassword(bPassword);
	}
}

UWidget* TSVanillaUI::MakeScrollBox(UWidgetTree* Tree, UWidget* Content)
{
	if (UUserWidget* Scroll = MakeVanilla(Tree, ScrollBoxClass))
	{
		Scroll->SetContentForSlot(TEXT("mScrollableContent"), Content);
		return Scroll;
	}
	UScrollBox* Plain = Tree->ConstructWidget<UScrollBox>();
	Plain->AddChild(Content);
	return Plain;
}

UTextBlock* TSVanillaUI::MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
	Block->SetText(Text);
	FSlateFontInfo Font = Block->GetFont();
	if (const FSlateFontInfo* Game = GameFont(Tree, bBold))
	{
		Font = *Game;
	}
	Font.Size = Size;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	return Block;
}
