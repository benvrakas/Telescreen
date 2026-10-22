#include "UI/TSScreensList.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Telescreen.h"
#include "TSConfig.h"
#include "TSRemoteCallObject.h"
#include "UI/TSScreenWindow.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	TWeakObjectPtr<UTSScreensList> GList;

	enum : uint8
	{
		ActionClose,
		ActionPin,
		ActionWindow,
	};

	UTextBlock* NewText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor::White)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(Text);
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}
}

void UTSScreensListAction::HandleClicked()
{
	if (List)
	{
		List->HandleAction(Screen.Get(), Action);
	}
}

void UTSScreensList::Toggle(const UObject* WorldContext)
{
	if (UTSScreensList* Open = GList.Get())
	{
		Open->CloseList();
		return;
	}
	APlayerController* Controller = UGameplayStatics::GetPlayerController(WorldContext, 0);
	UTSScreensList* List = Controller ? CreateWidget<UTSScreensList>(Controller, UTSScreensList::StaticClass()) : nullptr;
	if (!List)
	{
		return;
	}
	List->AddToViewport(20);
	List->SetAnchorsInViewport(FAnchors(0.5f, 0.5f));
	List->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	List->SetIsFocusable(true);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(List->TakeWidget());
	Controller->SetInputMode(Mode);
	Controller->SetShowMouseCursor(true);
	GList = List;
	UE_LOG(LogTelescreen, Log, TEXT("Screens list opened"));
}

void UTSScreensList::CloseList()
{
	if (APlayerController* Controller = GetOwningPlayer())
	{
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->SetShowMouseCursor(false);
	}
	RemoveFromParent();
	GList.Reset();
}

bool UTSScreensList::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return bSuperInitialized;
}

UWidget* UTSScreensList::MakeButton(const FText& Label, ATSBuildableScreen* Screen, uint8 Action)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(FLinearColor(0.12f, 0.13f, 0.16f));
	Button->AddChild(NewText(WidgetTree, Label, 13));
	UTSScreensListAction* Handler = NewObject<UTSScreensListAction>(this);
	Handler->List = this;
	Handler->Screen = Screen;
	Handler->Action = Action;
	Button->OnClicked.AddDynamic(Handler, &UTSScreensListAction::HandleClicked);
	Actions.Add(Handler);
	return Button;
}

void UTSScreensList::BuildTree()
{
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(760.f);
	Size->SetMaxDesiredHeight(640.f);
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.045f, 0.97f));
	Background->SetPadding(FMargin(20.f));
	Size->AddChild(Background);
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Background->AddChild(Root);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(NewText(WidgetTree, LOCTEXT("ScreensTitle", "Screens"), 24));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Header->AddChildToHorizontalBox(MakeButton(LOCTEXT("CloseList", "Close"), nullptr, ActionClose));
	Root->AddChildToVerticalBox(Header);

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Rows = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Rows);
	Root->AddChildToVerticalBox(Scroll)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));

	WidgetTree->RootWidget = Size;
}

void UTSScreensList::Rebuild()
{
	Rows->ClearChildren();
	TArray<ATSBuildableScreen*> Screens;
	for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
	{
		Screens.Add(*It);
	}
	const APlayerController* Controller = GetOwningPlayer();
	const FVector Viewer = Controller && Controller->GetPawn() ? Controller->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	Screens.Sort([&](const ATSBuildableScreen& A, const ATSBuildableScreen& B) { return FVector::DistSquared(A.GetActorLocation(), Viewer) < FVector::DistSquared(B.GetActorLocation(), Viewer); });
	if (Screens.Num() == 0)
	{
		Rows->AddChildToVerticalBox(NewText(WidgetTree, LOCTEXT("NoScreens", "There are no screens in this world yet."), 14));
		return;
	}
	for (ATSBuildableScreen* Screen : Screens)
	{
		FText What;
		if (Screen->GetMode() == ETSScreenMode::Browser)
		{
			What = FText::Format(LOCTEXT("ListBrowser", "Browser: {0}"), FText::FromString(Screen->GetControllerName()));
		}
		else if (const FTSMediaItem* Item = Screen->GetMediaState().GetCurrent())
		{
			What = FText::FromString(Item->GetDisplayTitle());
		}
		else
		{
			What = LOCTEXT("ListIdle", "Nothing playing");
		}
		const int32 Meters = FMath::RoundToInt(FVector::Dist(Screen->GetActorLocation(), Viewer) / 100.f);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
		Labels->AddChildToVerticalBox(NewText(WidgetTree, FText::Format(LOCTEXT("ListName", "{0}  ({1} m)"), Screen->GetDisplayName(), FText::AsNumber(Meters)), 15));
		Labels->AddChildToVerticalBox(NewText(WidgetTree, What, 12, FLinearColor(0.65f, 0.7f, 0.76f)));
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Labels);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		const bool bShown = UTSScreenWindow::IsShowing(Screen);
		Row->AddChildToHorizontalBox(MakeButton(bShown ? LOCTEXT("ListHide", "Remove from HUD") : LOCTEXT("ListShow", "Show on HUD"), Screen, ActionPin))
			->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		Row->AddChildToHorizontalBox(MakeButton(LOCTEXT("ListWindow", "Open window"), Screen, ActionWindow))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		Rows->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f));
	}
}

void UTSScreensList::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RebuildIn -= InDeltaTime;
	if (RebuildIn <= 0.f)
	{
		RebuildIn = 2.f;
		Rebuild();
	}
}

FReply UTSScreensList::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey ListKey(*UTSConfig::GetString(this, UTSConfig::ScreensListKeyKey, TEXT("F9")));
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == ListKey)
	{
		CloseList();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UTSScreensList::HandleAction(ATSBuildableScreen* Screen, uint8 Action)
{
	switch (Action)
	{
	case ActionClose:
		CloseList();
		break;
	case ActionPin:
		UTSScreenWindow::TogglePinned(Screen);
		RebuildIn = 0.f;
		break;
	case ActionWindow:
		CloseList();
		UTSScreenWindow::OpenInteractive(Screen);
		break;
	default:
		break;
	}
}

bool UTSClientSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UTSClientSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTSClientSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTSClientSubsystem, STATGROUP_Tickables);
}

void UTSClientSubsystem::Tick(float DeltaTime)
{
	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	const FKey FocusKey(*UTSConfig::GetString(this, UTSConfig::FocusKeyKey, TEXT("F8")));
	const FKey ListKey(*UTSConfig::GetString(this, UTSConfig::ScreensListKeyKey, TEXT("F9")));
	if (FocusKey.IsValid() && Controller->WasInputKeyJustPressed(FocusKey))
	{
		UTSScreenWindow::ToggleFocus(this);
	}
	if (ListKey.IsValid() && Controller->WasInputKeyJustPressed(ListKey))
	{
		UTSScreensList::Toggle(this);
	}
	const FKey CurtainKey(*UTSConfig::GetString(this, UTSConfig::CurtainKeyKey, TEXT("F10")));
	if (CurtainKey.IsValid() && Controller->WasInputKeyJustPressed(CurtainKey))
	{
		for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
		{
			if (It->IsLocallyControlled())
			{
				if (UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this))
				{
					RCO->Server_SetCurtain(*It, !It->IsCurtainOn());
				}
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
