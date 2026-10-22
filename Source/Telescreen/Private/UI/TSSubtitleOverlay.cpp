#include "UI/TSSubtitleOverlay.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "UI/TSVanillaUI.h"

namespace
{
	TWeakObjectPtr<UTSSubtitleOverlay> GSubtitleOverlay;
}

void UTSSubtitleOverlay::Update(const UObject* WorldContext, const FString& Text)
{
	UTSSubtitleOverlay* Overlay = GSubtitleOverlay.Get();
	if (Text.IsEmpty())
	{
		if (Overlay)
		{
			Overlay->RemoveFromParent();
			GSubtitleOverlay.Reset();
		}
		return;
	}
	if (!Overlay)
	{
		APlayerController* Controller = UGameplayStatics::GetPlayerController(WorldContext, 0);
		Overlay = Controller ? CreateWidget<UTSSubtitleOverlay>(Controller, UTSSubtitleOverlay::StaticClass()) : nullptr;
		if (!Overlay)
		{
			return;
		}
		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		Overlay->AddToViewport(4);
		GSubtitleOverlay = Overlay;
	}
	if (Overlay->Line)
	{
		Overlay->Line->SetText(FText::FromString(Text));
	}
}

bool UTSSubtitleOverlay::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		// The widget covers the viewport; the text sits bottom centre, above the hotbar.
		UOverlay* Layer = WidgetTree->ConstructWidget<UOverlay>();
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Background->SetPadding(FMargin(14.f, 6.f));
		Line = TSVanillaUI::MakeText(WidgetTree, FText::GetEmpty(), 20, FLinearColor::White);
		Line->SetJustification(ETextJustify::Center);
		Line->SetAutoWrapText(false);
		Background->AddChild(Line);
		UOverlaySlot* LineSlot = Layer->AddChildToOverlay(Background);
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetVerticalAlignment(VAlign_Bottom);
		LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 170.f));
		WidgetTree->RootWidget = Layer;
	}
	return bSuperInitialized;
}
