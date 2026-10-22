#include "UI/TSScreensList.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
#include "UI/TSVanillaUI.h"
#include "UI/TSStreamIndicator.h"
#include "UI/TSSubtitleOverlay.h"
#include "Camera/PlayerCameraManager.h"
#include "Screen/TSScreenPlayer.h"

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

	UTextBlock* NewText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bBold = false)
	{
		UTextBlock* Block = TSVanillaUI::MakeText(Tree, Text, Size, Color, bBold);
		Block->SetAutoWrapText(false);
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
	UTSScreensListAction* Handler = NewObject<UTSScreensListAction>(this);
	Handler->List = this;
	Handler->Screen = Screen;
	Handler->Action = Action;
	Actions.Add(Handler);
	return TSVanillaUI::MakeButton(WidgetTree, Label, Handler, GET_FUNCTION_NAME_CHECKED(UTSScreensListAction, HandleClicked));
}

void UTSScreensList::OnWindowClosed()
{
	CloseList();
}

void UTSScreensList::BuildTree()
{
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(780.f);
	USizeBox* ListSize = WidgetTree->ConstructWidget<USizeBox>();
	ListSize->SetMaxDesiredHeight(560.f);
	Rows = WidgetTree->ConstructWidget<UVerticalBox>();
	ListSize->AddChild(TSVanillaUI::MakeScrollBox(WidgetTree, Rows));
	Size->AddChild(TSVanillaUI::MakeWindow(WidgetTree, LOCTEXT("ScreensTitle", "Screens"), TSVanillaUI::MakeContentBackground(WidgetTree, ListSize), this,
		GET_FUNCTION_NAME_CHECKED(UTSScreensList, OnWindowClosed)));
	// The widget covers the viewport and centres the window in it.
	UOverlay* Layer = WidgetTree->ConstructWidget<UOverlay>();
	UOverlaySlot* WindowSlot = Layer->AddChildToOverlay(Size);
	WindowSlot->SetHorizontalAlignment(HAlign_Center);
	WindowSlot->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Layer;
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
		else if (Screen->GetMode() == ETSScreenMode::RemoteDesktop)
		{
			What = FText::Format(LOCTEXT("ListRemote", "Remote desktop shown by {0}"), FText::FromString(Screen->GetControllerName()));
		}
		else if (Screen->GetMode() == ETSScreenMode::Window)
		{
			What = FText::Format(LOCTEXT("ListWindow", "Window shared by {0}"), FText::FromString(Screen->GetControllerName()));
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
		Labels->AddChildToVerticalBox(NewText(WidgetTree, FText::Format(LOCTEXT("ListName", "{0}  ({1} m)"), Screen->GetDisplayName(), FText::AsNumber(Meters)), 15, FLinearColor::White, true));
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
	NextSubtitleUpdate -= DeltaTime;
	if (NextSubtitleUpdate <= 0.f)
	{
		NextSubtitleUpdate = 0.1f;
		UpdateHudSubtitles(Controller);
	}
	const FKey CurtainKey(*UTSConfig::GetString(this, UTSConfig::CurtainKeyKey, TEXT("F10")));
	const bool bCurtainPressed = CurtainKey.IsValid() && Controller->WasInputKeyJustPressed(CurtainKey);
	NextIndicatorUpdate -= DeltaTime;
	if (!bCurtainPressed && NextIndicatorUpdate > 0.f)
	{
		return;
	}
	NextIndicatorUpdate = 0.25f;
	ATSBuildableScreen* Mine = nullptr;
	ATSBuildableScreen* Granted = nullptr;
	for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
	{
		if (It->GetMode() != ETSScreenMode::SyncedMedia && It->IsLocallyControlled())
		{
			Mine = *It;
			break;
		}
		if (!Granted && It->GetMode() != ETSScreenMode::SyncedMedia && It->GetGrantFor(Controller->PlayerState) != ETSInputScope::None)
		{
			Granted = *It;
		}
	}
	if (bCurtainPressed && Mine)
	{
		if (UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this))
		{
			RCO->Server_SetCurtain(Mine, !Mine->IsCurtainOn());
		}
	}
	UTSStreamIndicator::Update(this, Mine ? Mine : Granted, Mine == nullptr && Granted != nullptr);
}

void UTSClientSubsystem::UpdateHudSubtitles(const APlayerController* Controller)
{
	const UTSScreenPlayer::ESubtitleMode Mode = UTSScreenPlayer::GetSubtitleMode(this);
	FString Text;
	if ((Mode == UTSScreenPlayer::ESubtitleMode::Hud || Mode == UTSScreenPlayer::ESubtitleMode::Both) && Controller->PlayerCameraManager)
	{
		const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
		const double Range = UTSConfig::GetFloat(this, UTSConfig::PlayDistanceKey, 150.f) * 100.0;
		double Best = Range;
		for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
		{
			const UTSScreenPlayer* Player = It->GetPlayer();
			if (!Player || Player->GetSubtitleText().IsEmpty())
			{
				continue;
			}
			if (Player->IsPinned())
			{
				Text = Player->GetSubtitleText();
				break;
			}
			const double Distance = FVector::Dist(Camera, It->GetScreenCenter());
			if (Distance <= Best)
			{
				Best = Distance;
				Text = Player->GetSubtitleText();
			}
		}
	}
	UTSSubtitleOverlay::Update(this, Text);
}

void UTSClientSubsystem::Deinitialize()
{
	UTSSubtitleOverlay::Update(this, FString());
	UTSStreamIndicator::Update(this, nullptr);
	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE
