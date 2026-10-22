#include "UI/TSStreamIndicator.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "TSConfig.h"
#include "UI/TSVanillaUI.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	TWeakObjectPtr<UTSStreamIndicator> GIndicator;
}

void UTSStreamIndicator::Update(const UObject* WorldContext, const ATSBuildableScreen* Screen, bool bGranted)
{
	UTSStreamIndicator* Indicator = GIndicator.Get();
	if (!Screen)
	{
		if (Indicator)
		{
			Indicator->RemoveFromParent();
			GIndicator.Reset();
		}
		return;
	}
	if (!Indicator)
	{
		APlayerController* Controller = UGameplayStatics::GetPlayerController(WorldContext, 0);
		Indicator = Controller ? CreateWidget<UTSStreamIndicator>(Controller, UTSStreamIndicator::StaticClass()) : nullptr;
		if (!Indicator)
		{
			return;
		}
		Indicator->SetVisibility(ESlateVisibility::HitTestInvisible);
		Indicator->AddToViewport(5);
		GIndicator = Indicator;
	}
	const FText What = Screen->GetMode() == ETSScreenMode::Window			? LOCTEXT("IndicatorWindow", "a window")
					 : Screen->GetMode() == ETSScreenMode::RemoteDesktop ? LOCTEXT("IndicatorRemote", "a remote desktop")
																		 : LOCTEXT("IndicatorBrowser", "your browser");
	const FText Key = FText::FromString(UTSConfig::GetString(Screen, UTSConfig::CurtainKeyKey, TEXT("F10")));
	FText Text;
	if (bGranted)
	{
		const APlayerController* Local = UGameplayStatics::GetPlayerController(WorldContext, 0);
		const FTSInputGrant* Grant = Local ? Screen->GetGrants().FindByPredicate([Local](const FTSInputGrant& Each) { return Each.Player == Local->PlayerState; }) : nullptr;
		const int32 Minutes = Grant ? FMath::Max(1, FMath::CeilToInt((Grant->ExpiresAt - Screen->GetServerTime()) / 60.0)) : 0;
		const FText Scope = Grant && Grant->Scope == ETSInputScope::Pointer ? LOCTEXT("ScopePointer", "the mouse")
			: Grant && Grant->Scope == ETSInputScope::Typing				  ? LOCTEXT("ScopeTyping", "mouse and typing")
																			  : LOCTEXT("ScopeFull", "mouse and keyboard");
		Text = FText::Format(LOCTEXT("IndicatorGranted", "{0} lets you use {1} on {2} ({3} min left): open it in a window ({4})"),
			FText::FromString(Screen->GetControllerName()), Scope, Screen->GetDisplayName(), FText::AsNumber(Minutes),
			FText::FromString(UTSConfig::GetString(Screen, UTSConfig::FocusKeyKey, TEXT("F8"))));
		if (Indicator->Line)
		{
			Indicator->Line->SetText(Text);
			Indicator->Line->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 1.f)));
		}
		return;
	}
	if (Screen->IsCurtainOn())
	{
		Text = FText::Format(LOCTEXT("IndicatorCurtain", "Sharing {0} on {1}: blanked for everyone ({2} shows it again)"), What, Screen->GetDisplayName(), Key);
	}
	else
	{
		Text = FText::Format(LOCTEXT("IndicatorLive", "● Sharing {0} on {1}: {2} watching ({3} blanks it)"), What, Screen->GetDisplayName(),
			FText::AsNumber(Screen->GetViewerCount()), Key);
	}
	if (Indicator->Line)
	{
		Indicator->Line->SetText(Text);
		Indicator->Line->SetColorAndOpacity(FSlateColor(Screen->IsCurtainOn() ? FLinearColor(0.7f, 0.75f, 0.8f) : FLinearColor(1.f, 0.42f, 0.32f)));
	}
}

bool UTSStreamIndicator::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		// The widget covers the viewport; the line is aligned top centre inside it (viewport-slot placement without a
		// size leaves widgets in the top-left corner).
		UOverlay* Layer = WidgetTree->ConstructWidget<UOverlay>();
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Background->SetPadding(FMargin(10.f, 4.f));
		Line = TSVanillaUI::MakeText(WidgetTree, FText::GetEmpty(), 12, FLinearColor::White);
		Line->SetAutoWrapText(false);
		Background->AddChild(Line);
		UOverlaySlot* LineSlot = Layer->AddChildToOverlay(Background);
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetVerticalAlignment(VAlign_Top);
		LineSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		WidgetTree->RootWidget = Layer;
	}
	return bSuperInitialized;
}

#undef LOCTEXT_NAMESPACE
