#include "UI/TSScreenWindow.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Host/TSHostProtocol.h"
#include "Kismet/GameplayStatics.h"
#include "Screen/TSScreenPlayer.h"
#include "Telescreen.h"
#include "TSConfig.h"

#define LOCTEXT_NAMESPACE "Telescreen"

using TSHostProtocol::EKeyType;

namespace
{
	TWeakObjectPtr<UTSScreenWindow> GWindow;
	constexpr float Margin = 24.f;
	constexpr float BottomMargin = 160.f;
	constexpr double EscapeHoldSeconds = 1.0;

	uint32 Modifiers(const FInputEvent& Event)
	{
		uint32 Bits = 0;
		if (Event.IsShiftDown()) Bits |= TSHostProtocol::ModShift;
		if (Event.IsControlDown()) Bits |= TSHostProtocol::ModControl;
		if (Event.IsAltDown()) Bits |= TSHostProtocol::ModAlt;
		return Bits;
	}

	uint32 PointerModifiers(const FPointerEvent& Event)
	{
		uint32 Bits = Modifiers(Event);
		if (Event.IsMouseButtonDown(EKeys::LeftMouseButton)) Bits |= TSHostProtocol::ModLeftButton;
		if (Event.IsMouseButtonDown(EKeys::MiddleMouseButton)) Bits |= TSHostProtocol::ModMiddleButton;
		if (Event.IsMouseButtonDown(EKeys::RightMouseButton)) Bits |= TSHostProtocol::ModRightButton;
		return Bits;
	}
}

UTSScreenWindow* UTSScreenWindow::Show(ATSBuildableScreen* InScreen)
{
	if (UTSScreenWindow* Existing = GWindow.Get())
	{
		if (Existing->Screen.Get() == InScreen)
		{
			return Existing;
		}
		Close();
	}
	APlayerController* Controller = InScreen ? UGameplayStatics::GetPlayerController(InScreen, 0) : nullptr;
	UTSScreenWindow* Window = Controller ? CreateWidget<UTSScreenWindow>(Controller, UTSScreenWindow::StaticClass()) : nullptr;
	if (!Window)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Could not create the screen window for %s"), *GetNameSafe(InScreen));
		return nullptr;
	}
	Window->Screen = InScreen;
	Window->SetVisibility(ESlateVisibility::HitTestInvisible);
	Window->AddToViewport(10);
	GWindow = Window;
	if (UTSScreenPlayer* Player = InScreen->GetPlayer())
	{
		Player->SetPinned(true);
	}
	Window->Place();
	return Window;
}

void UTSScreenWindow::TogglePinned(ATSBuildableScreen* InScreen)
{
	if (!InScreen)
	{
		return;
	}
	UTSScreenWindow* Existing = GWindow.Get();
	if (Existing && Existing->Screen.Get() == InScreen && Existing->bPinned && !Existing->bInteractive)
	{
		Close();
		return;
	}
	if (UTSScreenWindow* Window = Show(InScreen))
	{
		Window->bPinned = true;
		Window->SetInteractive(false);
		UE_LOG(LogTelescreen, Log, TEXT("%s shown on the HUD"), *InScreen->GetName());
	}
}

void UTSScreenWindow::OpenInteractive(ATSBuildableScreen* InScreen)
{
	if (UTSScreenWindow* Window = Show(InScreen))
	{
		Window->SetInteractive(true);
		UE_LOG(LogTelescreen, Log, TEXT("%s opened in the interactive window"), *InScreen->GetName());
	}
}

void UTSScreenWindow::ToggleFocus(const UObject* WorldContext)
{
	UTSScreenWindow* Window = GWindow.Get();
	if (Window && Window->bInteractive)
	{
		if (Window->bPinned)
		{
			Window->SetInteractive(false);
		}
		else
		{
			Close();
		}
		return;
	}
	// Only a source the local player controls takes input; a HUD picture of anything else stays a picture.
	if (Window && Window->GetBrowserPlayer())
	{
		Window->SetInteractive(true);
		return;
	}
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	for (TActorIterator<ATSBuildableScreen> It(World); World && It; ++It)
	{
		if (It->IsLocallyControlled())
		{
			OpenInteractive(*It);
			return;
		}
	}
}

bool UTSScreenWindow::IsShowing(const ATSBuildableScreen* InScreen)
{
	return GWindow.IsValid() && InScreen && GWindow->Screen.Get() == InScreen;
}

void UTSScreenWindow::Close()
{
	if (UTSScreenWindow* Window = GWindow.Get())
	{
		Window->SetInteractive(false);
		if (ATSBuildableScreen* Shown = Window->Screen.Get())
		{
			if (UTSScreenPlayer* Player = Shown->GetPlayer())
			{
				Player->SetPinned(false);
			}
			UE_LOG(LogTelescreen, Log, TEXT("%s removed from the HUD"), *Shown->GetName());
		}
		Window->RemoveFromParent();
	}
	GWindow.Reset();
}

bool UTSScreenWindow::Initialize()
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

void UTSScreenWindow::BuildTree()
{
	Size = WidgetTree->ConstructWidget<USizeBox>();
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	Frame->SetPadding(FMargin(3.f));
	Size->AddChild(Frame);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Frame->AddChild(Column);

	Hint = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo HintFont = Hint->GetFont();
	HintFont.Size = 11;
	Hint->SetFont(HintFont);
	Hint->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.55f, 0.1f)));
	Hint->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(Hint)->SetPadding(FMargin(6.f, 2.f, 6.f, 4.f));

	UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
	UVerticalBoxSlot* OverlaySlot = Column->AddChildToVerticalBox(Overlay);
	OverlaySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
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
	Title->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* TitleSlot = Overlay->AddChildToOverlay(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Left);
	TitleSlot->SetVerticalAlignment(VAlign_Top);
	TitleSlot->SetPadding(FMargin(6.f, 4.f));
	WidgetTree->RootWidget = Size;
}

void UTSScreenWindow::SetInteractive(bool bInInteractive)
{
	APlayerController* Controller = GetOwningPlayer();
	UTSScreenPlayer* Player = GetBrowserPlayer();
	const bool bWas = bInteractive;
	bInteractive = bInInteractive;
	if (bInteractive)
	{
		SetVisibility(ESlateVisibility::Visible);
		SetIsFocusable(true);
		if (Controller)
		{
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(Mode);
			Controller->SetShowMouseCursor(true);
		}
		SetKeyboardFocus();
		if (Player)
		{
			Player->BrowserFocus(true);
		}
	}
	else
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
		if (bHighPrecision && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ReleaseAllPointerCapture();
		}
		bHighPrecision = false;
		EscapeDownAt = 0.0;
		if (bWas && Controller)
		{
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetShowMouseCursor(false);
		}
		if (bWas && Player)
		{
			Player->BrowserFocus(false);
		}
	}
	const FString Key = UTSConfig::GetString(this, UTSConfig::FocusKeyKey, TEXT("F8"));
	Hint->SetText(FText::Format(LOCTEXT("WindowHint", "Using the screen: {0} to go back to the game (hold Esc also works)"), FText::FromString(Key)));
	Hint->SetVisibility(bInteractive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Place();
}

void UTSScreenWindow::NativeDestruct()
{
	if (bInteractive)
	{
		SetInteractive(false);
	}
	Super::NativeDestruct();
}

void UTSScreenWindow::Place()
{
	ATSBuildableScreen* Shown = Screen.Get();
	float Aspect = 2.f;
	if (ShownTexture && ShownTexture->GetSurfaceHeight() > 0.f)
	{
		Aspect = ShownTexture->GetSurfaceWidth() / ShownTexture->GetSurfaceHeight();
	}
	else if (Shown)
	{
		const FVector2D ScreenSize = Shown->GetScreenWorldSize();
		Aspect = ScreenSize.Y > 1.0 ? float(ScreenSize.X / ScreenSize.Y) : 2.f;
	}
	Aspect = FMath::Clamp(Aspect, 0.25f, 8.f);

	FVector2D NewSize, Anchor, Position;
	if (bInteractive)
	{
		const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.1f);
		const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this) / Scale;
		float Width = Viewport.X * 0.72f;
		if (Width / Aspect > Viewport.Y * 0.8f)
		{
			Width = Viewport.Y * 0.8f * Aspect;
		}
		NewSize = FVector2D(Width + 6.f, Width / Aspect + 26.f);
		Anchor = FVector2D(0.5f, 0.5f);
		Position = FVector2D::ZeroVector;
	}
	else
	{
		const float Width = FMath::Clamp(UTSConfig::GetFloat(this, UTSConfig::PipWidthKey, 420.f), 160.f, 1600.f);
		const int32 Corner = FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::PipCornerKey, 0), 0, 3);
		const bool bRight = Corner == 0 || Corner == 2;
		const bool bBottom = Corner == 0 || Corner == 1;
		NewSize = FVector2D(Width + 6.f, Width / Aspect + 6.f);
		Anchor = FVector2D(bRight ? 1.f : 0.f, bBottom ? 1.f : 0.f);
		Position = FVector2D(bRight ? -Margin : Margin, bBottom ? -BottomMargin : Margin);
	}
	if (NewSize.Equals(PlacedSize, 0.5f) && Anchor == PlacedAnchor && bPlacedInteractive == bInteractive)
	{
		return;
	}
	PlacedSize = NewSize;
	PlacedAnchor = Anchor;
	bPlacedInteractive = bInteractive;
	Size->SetWidthOverride(NewSize.X);
	Size->SetHeightOverride(NewSize.Y);
	SetAnchorsInViewport(FAnchors(Anchor.X, Anchor.Y));
	SetAlignmentInViewport(Anchor);
	SetPositionInViewport(Position, false);
}

UTSScreenPlayer* UTSScreenWindow::GetBrowserPlayer() const
{
	ATSBuildableScreen* Shown = Screen.Get();
	UTSScreenPlayer* Player = Shown ? Shown->GetPlayer() : nullptr;
	return Player && Player->AcceptsInput() ? Player : nullptr;
}

void UTSScreenWindow::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ATSBuildableScreen* Shown = Screen.Get();
	UTSScreenPlayer* Player = Shown ? Shown->GetPlayer() : nullptr;
	if (!Player)
	{
		Close();
		return;
	}
	// Control moved to someone else (or the browser closed): input no longer has anywhere to go.
	if (bInteractive && (!Shown->IsLocallyControlled() || Shown->GetMode() == ETSScreenMode::SyncedMedia))
	{
		UE_LOG(LogTelescreen, Log, TEXT("%s: no longer yours; leaving the interactive window"), *Shown->GetName());
		if (bPinned)
		{
			SetInteractive(false);
		}
		else
		{
			Close();
			return;
		}
	}
	UTexture* Texture = Player->GetDisplayTexture();
	if (Texture != ShownTexture)
	{
		ShownTexture = Texture;
		Picture->SetBrushResourceObject(Texture);
	}
	FText TitleText = Shown->GetDisplayName();
	if (Shown->IsLocallyControlled() && Shown->GetMode() == ETSScreenMode::Browser)
	{
		TitleText = Shown->IsCurtainOn() ? FText::Format(LOCTEXT("WindowCurtain", "{0}: blanked for everyone"), TitleText)
										 : FText::Format(LOCTEXT("WindowViewers", "{0}: {1} watching"), TitleText, FText::AsNumber(Shown->GetViewerCount()));
	}
	Title->SetText(bInteractive && !Shown->IsLocallyControlled() ? FText::GetEmpty() : TitleText);
	if (bInteractive && EscapeDownAt > 0.0 && FPlatformTime::Seconds() - EscapeDownAt > EscapeHoldSeconds)
	{
		EscapeDownAt = 0.0;
		if (bPinned)
		{
			SetInteractive(false);
		}
		else
		{
			Close();
			return;
		}
	}
	if (bInteractive)
	{
		UpdateCursor();
	}
	Place();
}

void UTSScreenWindow::UpdateCursor()
{
	const UTSScreenPlayer* Player = GetBrowserPlayer();
	EMouseCursor::Type Wanted = EMouseCursor::Default;
	if (Player)
	{
		switch (Player->GetBrowserState().Cursor)
		{
		case 1: Wanted = EMouseCursor::Crosshairs; break;
		case 2: Wanted = EMouseCursor::Hand; break;
		case 3: Wanted = EMouseCursor::TextEditBeam; break;
		default: break;
		}
	}
	if (GetCursor() != Wanted)
	{
		SetCursor(Wanted);
	}
}

bool UTSScreenWindow::PicturePosition(const FPointerEvent& Event, FVector2D& OutPosition) const
{
	const FGeometry& Geometry = Picture->GetCachedGeometry();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FVector2D LocalSize = Geometry.GetLocalSize();
	if (LocalSize.X < 1.f || LocalSize.Y < 1.f)
	{
		return false;
	}
	OutPosition = Local / LocalSize;
	return OutPosition.X >= 0.f && OutPosition.Y >= 0.f && OutPosition.X <= 1.f && OutPosition.Y <= 1.f;
}

FReply UTSScreenWindow::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UTSScreenPlayer* Player = GetBrowserPlayer();
	if (!bInteractive || !Player)
	{
		return FReply::Unhandled();
	}
	if (Player->HasBrowser() && Player->GetBrowserState().bPointerLocked)
	{
		const FVector2D Delta = InMouseEvent.GetCursorDelta();
		Player->BrowserPointerDelta(FMath::RoundToInt(Delta.X), FMath::RoundToInt(Delta.Y));
		if (!bHighPrecision)
		{
			bHighPrecision = true;
			return FReply::Handled().CaptureMouse(TakeWidget()).UseHighPrecisionMouseMovement(TakeWidget());
		}
		return FReply::Handled();
	}
	FReply Reply = FReply::Handled();
	if (bHighPrecision)
	{
		bHighPrecision = false;
		Reply = Reply.ReleaseMouseCapture();
	}
	FVector2D Position;
	if (PicturePosition(InMouseEvent, Position))
	{
		Player->BrowserMouseMove(Position, PointerModifiers(InMouseEvent));
	}
	return Reply;
}

FReply UTSScreenWindow::HandleButton(const FPointerEvent& Event, bool bUp, int32 ClickCount)
{
	UTSScreenPlayer* Player = GetBrowserPlayer();
	if (!bInteractive || !Player)
	{
		return FReply::Unhandled();
	}
	const FKey Key = Event.GetEffectingButton();
	const uint8 Button = Key == EKeys::MiddleMouseButton ? 1 : Key == EKeys::RightMouseButton ? 2 : 0;
	FVector2D Position(0.5f, 0.5f);
	if (!PicturePosition(Event, Position) && !Player->GetBrowserState().bPointerLocked)
	{
		return FReply::Handled();
	}
	Player->BrowserMouseButton(Position, Button, bUp, ClickCount, PointerModifiers(Event));
	return FReply::Handled().SetUserFocus(TakeWidget());
}

FReply UTSScreenWindow::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandleButton(InMouseEvent, false, 1);
}

FReply UTSScreenWindow::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandleButton(InMouseEvent, true, 1);
}

FReply UTSScreenWindow::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return HandleButton(InMouseEvent, false, 2);
}

FReply UTSScreenWindow::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UTSScreenPlayer* Player = GetBrowserPlayer();
	FVector2D Position;
	if (!bInteractive || !Player || !PicturePosition(InMouseEvent, Position))
	{
		return bInteractive ? FReply::Handled() : FReply::Unhandled();
	}
	Player->BrowserWheel(Position, 0, FMath::RoundToInt(InMouseEvent.GetWheelDelta() * 120.f), PointerModifiers(InMouseEvent));
	return FReply::Handled();
}

void UTSScreenWindow::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (UTSScreenPlayer* Player = GetBrowserPlayer(); Player && bInteractive && !bHighPrecision)
	{
		Player->BrowserMouseLeave();
	}
}

bool UTSScreenWindow::IsFocusKey(const FKey& Key) const
{
	const FKey FocusKey(*UTSConfig::GetString(this, UTSConfig::FocusKeyKey, TEXT("F8")));
	return FocusKey.IsValid() && Key == FocusKey;
}

FReply UTSScreenWindow::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bInteractive)
	{
		return FReply::Unhandled();
	}
	if (IsFocusKey(InKeyEvent.GetKey()))
	{
		if (bPinned)
		{
			SetInteractive(false);
		}
		else
		{
			Close();
		}
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape && EscapeDownAt == 0.0)
	{
		EscapeDownAt = FPlatformTime::Seconds();
	}
	if (UTSScreenPlayer* Player = GetBrowserPlayer())
	{
		Player->BrowserKey(EKeyType::RawKeyDown, int32(InKeyEvent.GetKeyCode()), 0, Modifiers(InKeyEvent));
	}
	return FReply::Handled();
}

FReply UTSScreenWindow::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bInteractive)
	{
		return FReply::Unhandled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		EscapeDownAt = 0.0;
	}
	if (UTSScreenPlayer* Player = GetBrowserPlayer(); Player && !IsFocusKey(InKeyEvent.GetKey()))
	{
		Player->BrowserKey(EKeyType::KeyUp, int32(InKeyEvent.GetKeyCode()), 0, Modifiers(InKeyEvent));
	}
	return FReply::Handled();
}

FReply UTSScreenWindow::NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent)
{
	if (!bInteractive)
	{
		return FReply::Unhandled();
	}
	if (UTSScreenPlayer* Player = GetBrowserPlayer())
	{
		const TCHAR Character = InCharEvent.GetCharacter();
		Player->BrowserKey(EKeyType::Char, int32(Character), Character, Modifiers(InCharEvent));
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
