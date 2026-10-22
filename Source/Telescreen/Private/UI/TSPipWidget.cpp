#include "UI/TSPipWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture.h"
#include "Kismet/GameplayStatics.h"
#include "Screen/TSScreenPlayer.h"
#include "Telescreen.h"
#include "TSConfig.h"

namespace
{
	TWeakObjectPtr<UTSPipWidget> GPip;
	constexpr float Margin = 24.f;
	constexpr float BottomMargin = 160.f;
}

void UTSPipWidget::Toggle(ATSBuildableScreen* InScreen)
{
	if (!InScreen)
	{
		return;
	}
	const bool bWasThis = IsPinned(InScreen);
	Unpin();
	if (bWasThis)
	{
		return;
	}
	APlayerController* Controller = UGameplayStatics::GetPlayerController(InScreen, 0);
	UTSPipWidget* Widget = Controller ? CreateWidget<UTSPipWidget>(Controller, UTSPipWidget::StaticClass()) : nullptr;
	if (!Widget)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Could not create the picture-in-picture for %s"), *InScreen->GetName());
		return;
	}
	Widget->Screen = InScreen;
	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	Widget->AddToViewport(5);
	Widget->Place();
	GPip = Widget;
	if (UTSScreenPlayer* Player = InScreen->GetPlayer())
	{
		Player->SetPinned(true);
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s pinned to the HUD"), *InScreen->GetName());
}

bool UTSPipWidget::IsPinned(const ATSBuildableScreen* InScreen)
{
	return GPip.IsValid() && InScreen && GPip->Screen.Get() == InScreen;
}

void UTSPipWidget::Unpin()
{
	if (UTSPipWidget* Widget = GPip.Get())
	{
		if (ATSBuildableScreen* Pinned = Widget->Screen.Get())
		{
			if (UTSScreenPlayer* Player = Pinned->GetPlayer())
			{
				Player->SetPinned(false);
			}
			UE_LOG(LogTelescreen, Log, TEXT("%s unpinned from the HUD"), *Pinned->GetName());
		}
		Widget->RemoveFromParent();
	}
	GPip.Reset();
}

bool UTSPipWidget::Initialize()
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

void UTSPipWidget::BuildTree()
{
	Size = WidgetTree->ConstructWidget<USizeBox>();
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Frame->SetPadding(FMargin(3.f));
	Size->AddChild(Frame);
	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	Frame->AddChild(Overlay);
	Picture = WidgetTree->ConstructWidget<UImage>();
	UOverlaySlot* PictureSlot = Overlay->AddChildToOverlay(Picture);
	PictureSlot->SetHorizontalAlignment(HAlign_Fill);
	PictureSlot->SetVerticalAlignment(VAlign_Fill);
	Title = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo Font = Title->GetFont();
	Font.Size = 11;
	Title->SetFont(Font);
	Title->SetShadowOffset(FVector2D(1.f, 1.f));
	Title->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	UOverlaySlot* TitleSlot = Overlay->AddChildToOverlay(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Left);
	TitleSlot->SetVerticalAlignment(VAlign_Top);
	TitleSlot->SetPadding(FMargin(6.f, 4.f));
	WidgetTree->RootWidget = Size;
}

void UTSPipWidget::Place()
{
	ATSBuildableScreen* Pinned = Screen.Get();
	const float Width = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::PipWidthKey, 420.f), 160.f, 1600.f);
	const int32 Corner = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::PipCornerKey, 0), 0, 3);
	float Aspect = 2.f;
	if (ShownTexture && ShownTexture->GetSurfaceHeight() > 0.f)
	{
		Aspect = ShownTexture->GetSurfaceWidth() / ShownTexture->GetSurfaceHeight();
	}
	else if (Pinned)
	{
		const FVector2D ScreenSize = Pinned->GetScreenWorldSize();
		Aspect = ScreenSize.Y > 1.0 ? float(ScreenSize.X / ScreenSize.Y) : 2.f;
	}
	if (FMath::IsNearlyEqual(Width, PlacedWidth) && FMath::IsNearlyEqual(Aspect, PlacedAspect, 0.01f) && Corner == PlacedCorner)
	{
		return;
	}
	PlacedWidth = Width;
	PlacedAspect = Aspect;
	PlacedCorner = Corner;

	// Corners: 0 bottom right, 1 bottom left, 2 top right, 3 top left.
	const bool bRight = Corner == 0 || Corner == 2;
	const bool bBottom = Corner == 0 || Corner == 1;
	const FVector2D Anchor(bRight ? 1.f : 0.f, bBottom ? 1.f : 0.f);
	const float Height = Width / FMath::Max(Aspect, 0.1f) + 6.f;
	Size->SetWidthOverride(Width + 6.f);
	Size->SetHeightOverride(Height);
	SetAnchorsInViewport(FAnchors(Anchor.X, Anchor.Y));
	SetAlignmentInViewport(Anchor);
	SetPositionInViewport(FVector2D(bRight ? -Margin : Margin, bBottom ? -BottomMargin : Margin), false);
}

void UTSPipWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ATSBuildableScreen* Pinned = Screen.Get();
	UTSScreenPlayer* Player = Pinned ? Pinned->GetPlayer() : nullptr;
	if (!Player)
	{
		Unpin();
		return;
	}
	UTexture* Texture = Player->GetDisplayTexture();
	if (Texture != ShownTexture)
	{
		ShownTexture = Texture;
		Picture->SetBrushResourceObject(Texture);
	}
	Title->SetText(Pinned->GetDisplayName());
	Place();
}
