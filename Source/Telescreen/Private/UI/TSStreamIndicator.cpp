#include "UI/TSStreamIndicator.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "TSConfig.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	TWeakObjectPtr<UTSStreamIndicator> GIndicator;
}

void UTSStreamIndicator::Update(const UObject* WorldContext, const ATSBuildableScreen* Screen)
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
		Indicator->SetAnchorsInViewport(FAnchors(0.5f, 0.f));
		Indicator->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
		Indicator->SetPositionInViewport(FVector2D(0.f, 12.f), false);
		GIndicator = Indicator;
	}
	const FText What = Screen->GetMode() == ETSScreenMode::Window ? LOCTEXT("IndicatorWindow", "a window") : LOCTEXT("IndicatorBrowser", "your browser");
	const FText Key = FText::FromString(UTSConfig::GetString(Screen, UTSConfig::CurtainKeyKey, TEXT("F10")));
	FText Text;
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
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Background->SetPadding(FMargin(10.f, 4.f));
		Line = WidgetTree->ConstructWidget<UTextBlock>();
		FSlateFontInfo Font = Line->GetFont();
		Font.Size = 12;
		Line->SetFont(Font);
		Background->AddChild(Line);
		WidgetTree->RootWidget = Background;
	}
	return bSuperInitialized;
}

#undef LOCTEXT_NAMESPACE
