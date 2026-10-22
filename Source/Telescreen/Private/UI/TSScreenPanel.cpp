#include "UI/TSScreenPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "EngineUtils.h"
#include "FGHUD.h"
#include "GameFramework/PlayerController.h"
#include "Screen/TSScreenPlayer.h"
#include "Telescreen.h"
#include "TSConfig.h"
#include "TSRemoteCallObject.h"
#include "UI/FGGameUI.h"
#include "UI/TSScreenWindow.h"
#include "UI/TSVanillaUI.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	const FLinearColor Accent(0.95f, 0.55f, 0.1f);
	const FLinearColor Muted(0.65f, 0.7f, 0.76f);
	const FLinearColor ErrorColor(1.f, 0.45f, 0.4f);
	// The interactive window opens if control arrives within this time after asking for the browser.
	constexpr double OpenWindowTimeout = 10.0;

	FText FormatTime(double Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
		const int32 Hours = Total / 3600, Minutes = (Total / 60) % 60, Secs = Total % 60;
		return FText::FromString(Hours > 0 ? FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Secs) : FString::Printf(TEXT("%d:%02d"), Minutes, Secs));
	}

	FText AspectName(ETSAspectMode Mode)
	{
		switch (Mode)
		{
		case ETSAspectMode::Fill: return LOCTEXT("AspectFill", "Fill");
		case ETSAspectMode::Stretch: return LOCTEXT("AspectStretch", "Stretch");
		default: return LOCTEXT("AspectLetterbox", "Letterbox");
		}
	}

	void Show(UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

void UTSPanelAction::HandleClicked()
{
	if (Panel)
	{
		Panel->HandleAction(Action, Index);
	}
}

void UTSPanelAction::HandleCommitted(const FText& Text, ETextCommit::Type Method)
{
	if (Panel && Method == ETextCommit::OnEnter)
	{
		Panel->HandleAction(Action, Index);
	}
}

UTSScreenPanel::UTSScreenPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	mUseKeyboard = true;
	mUseMouse = true;
	mDesiredHorizontalAlignment = HAlign_Center;
	mDesiredVerticalAlignment = VAlign_Center;
}

bool UTSScreenPanel::Initialize()
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

UTextBlock* UTSScreenPanel::MakeText(const FText& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
	return TSVanillaUI::MakeText(WidgetTree, Text, Size, Color, bBold);
}

UTSPanelAction* UTSScreenPanel::MakeHandler(ETSPanelAction Action, int32 Index)
{
	UTSPanelAction* Handler = NewObject<UTSPanelAction>(this);
	Handler->Panel = this;
	Handler->Action = Action;
	Handler->Index = Index;
	Actions.Add(Handler);
	return Handler;
}

UWidget* UTSScreenPanel::MakeButton(const FText& Label, ETSPanelAction Action, int32 Index, bool bPrimary)
{
	return TSVanillaUI::MakeButton(WidgetTree, Label, MakeHandler(Action, Index), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleClicked),
		bPrimary ? TSVanillaUI::EButtonStyle::Primary : TSVanillaUI::EButtonStyle::Normal);
}

UWidget* UTSScreenPanel::MakeInput(const FText& Hint, ETSPanelAction OnEnter)
{
	return TSVanillaUI::MakeInputBox(WidgetTree, Hint, MakeHandler(OnEnter, INDEX_NONE), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleCommitted));
}

void UTSScreenPanel::OnWindowClosed()
{
	Close();
}

void UTSScreenPanel::BuildTree()
{
	auto AddRow = [](UVerticalBox* Box, UWidget* Child, float Top = 6.f)
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
		Slot->SetPadding(FMargin(0.f, Top, 0.f, 0.f));
		return Slot;
	};
	auto MakeHBox = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(); };
	auto MakeVBox = [this]() { return WidgetTree->ConstructWidget<UVerticalBox>(); };
	auto AddToHBox = [](UHorizontalBox* Box, UWidget* Child, bool bFill = false)
	{
		UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Child);
		Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		Slot->SetVerticalAlignment(VAlign_Center);
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Slot;
	};

	// The vanilla window frame (title = screen name, close button) around the content background; the confirm dialog
	// overlays the content.
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	UVerticalBox* Root = MakeVBox();
	Layers->AddChildToOverlay(Root);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(900.f);
	Window = TSVanillaUI::MakeWindow(WidgetTree, LOCTEXT("PanelTitle", "Telescreen"), TSVanillaUI::MakeContentBackground(WidgetTree, Layers), this,
		GET_FUNCTION_NAME_CHECKED(UTSScreenPanel, OnWindowClosed));
	Size->AddChild(Window);
	WidgetTree->RootWidget = Size;

	// Header: what the screen shows, Show on HUD; the sharing bar; server notices.
	UHorizontalBox* Header = MakeHBox();
	StatusText = MakeText(FText::GetEmpty(), 17, FLinearColor::White, true);
	AddToHBox(Header, StatusText, true);
	PipButton = MakeButton(LOCTEXT("ShowOnHud", "Show on HUD"), ETSPanelAction::TogglePip);
	AddToHBox(Header, PipButton);
	AddRow(Root, Header, 0.f);
	TimeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Root, TimeText, 2.f);

	UVerticalBox* Sharing = MakeVBox();
	SharingText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Sharing, SharingText, 0.f);
	UHorizontalBox* SharingButtons = MakeHBox();
	OpenWindowButton = MakeButton(LOCTEXT("OpenWindow", "Use it in a window"), ETSPanelAction::OpenWindow, INDEX_NONE, true);
	AddToHBox(SharingButtons, OpenWindowButton);
	CurtainButton = MakeButton(LOCTEXT("CurtainOn", "Blank for everyone"), ETSPanelAction::CurtainToggle);
	AddToHBox(SharingButtons, CurtainButton);
	StopSharingButton = MakeButton(LOCTEXT("StopSharing", "Stop"), ETSPanelAction::StopSharing);
	AddToHBox(SharingButtons, StopSharingButton);
	TakeOverButton = MakeButton(LOCTEXT("TakeOver", "Use the browser myself"), ETSPanelAction::BrowserTakeOver);
	AddToHBox(SharingButtons, TakeOverButton);
	HostStopButton = MakeButton(LOCTEXT("HostStop", "Stop it (host)"), ETSPanelAction::HostStop);
	AddToHBox(SharingButtons, HostStopButton);
	AddRow(Sharing, SharingButtons, 6.f);
	SharingBar = Sharing;
	AddRow(Root, Sharing, 8.f);

	// Tabs.
	UHorizontalBox* TabRow = MakeHBox();
	const FText TabNames[] = {LOCTEXT("TabVideos", "Videos"), LOCTEXT("TabBrowser", "Browser"), LOCTEXT("TabWindow", "Share a window"),
		LOCTEXT("TabScreen", "Screen")};
	for (int32 I = 0; I < UE_ARRAY_COUNT(TabNames); ++I)
	{
		UWidget* Tab = TSVanillaUI::MakeTab(WidgetTree, TabNames[I], MakeHandler(ETSPanelAction::Tab, I), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleClicked));
		TabButtons.Add(Tab);
		AddToHBox(TabRow, Tab);
	}
	AddRow(Root, TabRow, 14.f);
	NoticeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Root, NoticeText, 6.f);

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	USizeBox* PagesSize = WidgetTree->ConstructWidget<USizeBox>();
	PagesSize->SetMaxDesiredHeight(500.f);
	PagesSize->AddChild(TSVanillaUI::MakeScrollBox(WidgetTree, Pages));
	AddRow(Root, PagesSize, 6.f);

	// Videos.
	{
		UVerticalBox* Page = MakeVBox();
		UHorizontalBox* Transport = MakeHBox();
		AddToHBox(Transport, MakeButton(LOCTEXT("PlayPause", "Play / Pause"), ETSPanelAction::PlayPause, INDEX_NONE, true));
		AddToHBox(Transport, MakeButton(LOCTEXT("Back10", "-10 s"), ETSPanelAction::Back10));
		AddToHBox(Transport, MakeButton(LOCTEXT("Forward10", "+10 s"), ETSPanelAction::Forward10));
		AddToHBox(Transport, MakeButton(LOCTEXT("Next", "Next"), ETSPanelAction::Next));
		AddToHBox(Transport, MakeButton(LOCTEXT("Stop", "Stop"), ETSPanelAction::Stop));
		AddRow(Page, Transport, 0.f);
		AddRow(Page, MakeText(LOCTEXT("AddHeader", "Add a video"), 14, Accent, true), 14.f);
		UHorizontalBox* AddBox = MakeHBox();
		LinkBox = MakeInput(LOCTEXT("LinkHint", "Paste a YouTube, Twitch or Vimeo link, or a video file path"), ETSPanelAction::PlayNow);
		AddToHBox(AddBox, LinkBox, true);
		AddToHBox(AddBox, MakeButton(LOCTEXT("PlayNow", "Play now"), ETSPanelAction::PlayNow, INDEX_NONE, true));
		AddToHBox(AddBox, MakeButton(LOCTEXT("AddToQueue", "Add to queue"), ETSPanelAction::AddToQueue));
		AddRow(Page, AddBox);
		LoadAnywayButton = MakeButton(LOCTEXT("LoadAnyway", "Load it anyway on this PC"), ETSPanelAction::LoadAnyway);
		Show(LoadAnywayButton, false);
		AddRow(Page, LoadAnywayButton, 4.f);
		UHorizontalBox* QueueHeader = MakeHBox();
		AddToHBox(QueueHeader, MakeText(LOCTEXT("QueueHeader", "Queue"), 14, Accent, true), true);
		AddToHBox(QueueHeader, MakeButton(LOCTEXT("ClearQueue", "Clear"), ETSPanelAction::ClearQueue));
		AddRow(Page, QueueHeader, 14.f);
		QueueBox = MakeVBox();
		AddRow(Page, QueueBox, 4.f);
		Pages->AddChild(Page);
	}

	// Browser.
	{
		UVerticalBox* Page = MakeVBox();
		UHorizontalBox* UrlRow = MakeHBox();
		BrowserUrlBox = MakeInput(LOCTEXT("BrowserUrlHint", "Web address, or leave empty for your start page"), ETSPanelAction::BrowserGo);
		AddToHBox(UrlRow, BrowserUrlBox, true);
		BrowserOpenButton = MakeButton(LOCTEXT("BrowserOpen", "Open the browser"), ETSPanelAction::BrowserOpen, INDEX_NONE, true);
		AddToHBox(UrlRow, BrowserOpenButton);
		UHorizontalBox* NavRow = MakeHBox();
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserGo", "Go"), ETSPanelAction::BrowserGo, INDEX_NONE, true));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserBack", "Back"), ETSPanelAction::BrowserBack));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserForward", "Forward"), ETSPanelAction::BrowserForward));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserReload", "Reload"), ETSPanelAction::BrowserReload));
		BrowserNavRow = NavRow;
		AddToHBox(UrlRow, NavRow);
		AddRow(Page, UrlRow, 0.f);
		BrowserNote = MakeText(FText::GetEmpty(), 12, Muted);
		AddRow(Page, BrowserNote, 6.f);
		UHorizontalBox* Loopback = MakeHBox();
		LoopbackText = MakeText(FText::GetEmpty(), 12, Muted);
		AddToHBox(Loopback, LoopbackText, true);
		AddToHBox(Loopback, MakeButton(LOCTEXT("LoopbackToggle", "Stream test"), ETSPanelAction::LoopbackToggle));
		LoopbackRow = Loopback;
		AddRow(Page, Loopback, 10.f);
		Pages->AddChild(Page);
	}

	// Share a window.
	{
		UVerticalBox* Page = MakeVBox();
		UHorizontalBox* Top = MakeHBox();
		WindowNote = MakeText(FText::GetEmpty(), 13, Muted);
		AddToHBox(Top, WindowNote, true);
		AddToHBox(Top, MakeButton(LOCTEXT("WindowRefresh", "Refresh list"), ETSPanelAction::WindowRefresh));
		AddRow(Page, Top, 0.f);
		WindowListBox = MakeVBox();
		AddRow(Page, WindowListBox, 8.f);
		Pages->AddChild(Page);
	}

	// Screen.
	{
		UVerticalBox* Page = MakeVBox();
		AddRow(Page, MakeText(LOCTEXT("NameHeader", "Name"), 14, Accent, true), 0.f);
		UHorizontalBox* NameRow = MakeHBox();
		NameBox = MakeInput(LOCTEXT("NameHint", "Screen name, e.g. Main Hall"), ETSPanelAction::SaveName);
		AddToHBox(NameRow, NameBox, true);
		AddToHBox(NameRow, MakeButton(LOCTEXT("SaveName", "Save name"), ETSPanelAction::SaveName));
		AddRow(Page, NameRow);
		AddRow(Page, MakeText(LOCTEXT("FitHeader", "Picture fit (videos and shared windows)"), 14, Accent, true), 14.f);
		UHorizontalBox* AspectRow = MakeHBox();
		AspectText = MakeText(FText::GetEmpty(), 13, Muted);
		AddToHBox(AspectRow, AspectText, true);
		AddToHBox(AspectRow, MakeButton(LOCTEXT("Letterbox", "Letterbox"), ETSPanelAction::AspectLetterbox));
		AddToHBox(AspectRow, MakeButton(LOCTEXT("Fill", "Fill"), ETSPanelAction::AspectFill));
		AddToHBox(AspectRow, MakeButton(LOCTEXT("Stretch", "Stretch"), ETSPanelAction::AspectStretch));
		AddRow(Page, AspectRow);
		Pages->AddChild(Page);
	}

	// Confirm dialog over the whole panel.
	UBorder* Shade = WidgetTree->ConstructWidget<UBorder>();
	Shade->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Shade->SetHorizontalAlignment(HAlign_Center);
	Shade->SetVerticalAlignment(VAlign_Center);
	USizeBox* DialogSize = WidgetTree->ConstructWidget<USizeBox>();
	DialogSize->SetWidthOverride(560.f);
	UVerticalBox* DialogBox = MakeVBox();
	ConfirmTitle = MakeText(FText::GetEmpty(), 18, Accent, true);
	AddRow(DialogBox, ConfirmTitle, 0.f);
	ConfirmText = MakeText(FText::GetEmpty(), 13);
	AddRow(DialogBox, ConfirmText, 10.f);
	UHorizontalBox* DialogButtons = MakeHBox();
	AddToHBox(DialogButtons, MakeButton(LOCTEXT("ConfirmAccept", "I understand, continue"), ETSPanelAction::ConfirmAccept, INDEX_NONE, true));
	AddToHBox(DialogButtons, MakeButton(LOCTEXT("ConfirmCancel", "Cancel"), ETSPanelAction::ConfirmCancel));
	AddRow(DialogBox, DialogButtons, 14.f);
	DialogSize->AddChild(TSVanillaUI::MakeContentBackground(WidgetTree, DialogBox));
	Shade->AddChild(DialogSize);
	UOverlaySlot* ShadeSlot = Layers->AddChildToOverlay(Shade);
	ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
	ShadeSlot->SetVerticalAlignment(VAlign_Fill);
	ConfirmLayer = Shade;
	Show(ConfirmLayer, false);
}

ATSBuildableScreen* UTSScreenPanel::GetScreen() const
{
	return Cast<ATSBuildableScreen>(mInteractObject);
}

void UTSScreenPanel::NativeConstruct()
{
	Super::NativeConstruct();
	ATSBuildableScreen* Screen = GetScreen();
	if (Screen)
	{
		ScreenChangedHandle = Screen->OnScreenChanged.AddUObject(this, &UTSScreenPanel::Refresh);
		TSVanillaUI::SetInputText(NameBox, FText::FromString(Screen->GetScreenName()));
	}
	NoticeHandle = UTSRemoteCallObject::OnNotice.AddWeakLambda(this, [this](const FText& Text) { SetNotice(Text, true); });
	// Open on the tab for what the screen does now.
	const ETSScreenMode Mode = Screen ? Screen->GetMode() : ETSScreenMode::SyncedMedia;
	SelectTab(Mode == ETSScreenMode::Browser ? ETab::Browser : Mode == ETSScreenMode::Window ? ETab::Window : ETab::Videos);
	Refresh();
	UE_LOG(LogTelescreen, Log, TEXT("Screen panel opened for %s"), *GetNameSafe(Screen));
}

void UTSScreenPanel::NativeDestruct()
{
	if (ATSBuildableScreen* Screen = GetScreen())
	{
		Screen->OnScreenChanged.Remove(ScreenChangedHandle);
		if (UTSScreenPlayer* Player = Screen->GetPlayer())
		{
			Player->OnBrowserStateChanged.Remove(BrowserStateHandle);
		}
	}
	UTSRemoteCallObject::OnNotice.Remove(NoticeHandle);
	Super::NativeDestruct();
}

void UTSScreenPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TimeRefresh -= InDeltaTime;
	if (TimeRefresh <= 0.f)
	{
		TimeRefresh = 0.25f;
		RefreshTimes();
	}
}

void UTSScreenPanel::OnEscapePressed_Implementation()
{
	if (PendingConfirm != EConfirm::None)
	{
		HandleAction(ETSPanelAction::ConfirmCancel, INDEX_NONE);
		return;
	}
	Close();
}

void UTSScreenPanel::Close()
{
	APlayerController* Controller = GetOwningPlayer();
	AFGHUD* Hud = Controller ? Cast<AFGHUD>(Controller->GetHUD()) : nullptr;
	if (UFGGameUI* GameUI = Hud ? Hud->GetGameUI() : nullptr)
	{
		GameUI->PopWidget(this);
	}
	else
	{
		RemoveFromParent();
	}
}

void UTSScreenPanel::SelectTab(ETab Tab)
{
	CurrentTab = Tab;
	Pages->SetActiveWidgetIndex(int32(Tab));
	for (int32 I = 0; I < TabButtons.Num(); ++I)
	{
		TSVanillaUI::SetTabActive(TabButtons[I], I == int32(Tab));
	}
	if (Tab == ETab::Window && Windows.Num() == 0)
	{
		RequestWindowList();
	}
	RefreshSharing();
}

void UTSScreenPanel::Refresh()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	if (!Screen->GetDisplayName().EqualTo(ShownTitle))
	{
		ShownTitle = Screen->GetDisplayName();
		TSVanillaUI::SetWindowTitle(Window, ShownTitle);
	}
	AspectText->SetText(FText::Format(LOCTEXT("AspectNow", "Now: {0}"), AspectName(Screen->GetAspectMode())));
	TSVanillaUI::SetButtonText(PipButton, UTSScreenWindow::IsShowing(Screen) ? LOCTEXT("RemoveFromHud", "Remove from HUD") : LOCTEXT("ShowOnHud", "Show on HUD"));
	if (Screen->GetMediaState().Revision != ShownRevision)
	{
		ShownRevision = Screen->GetMediaState().Revision;
		RebuildQueue();
	}
	RefreshTimes();

	// The browser this player asked for is now theirs: go straight to its window.
	if (OpenWindowWhenMineUntil > 0.0 && Screen->GetMode() == ETSScreenMode::Browser && Screen->IsLocallyControlled())
	{
		OpenWindowWhenMineUntil = 0.0;
		UE_LOG(LogTelescreen, Log, TEXT("%s: browser is ours; opening its window"), *Screen->GetName());
		Close();
		UTSScreenWindow::OpenInteractive(Screen);
	}
}

void UTSScreenPanel::RefreshTimes()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	if (OpenWindowWhenMineUntil > 0.0 && FPlatformTime::Seconds() > OpenWindowWhenMineUntil)
	{
		OpenWindowWhenMineUntil = 0.0;
	}
	const FTSMediaState& State = Screen->GetMediaState();
	const FTSMediaItem* Item = State.GetCurrent();
	const UTSScreenPlayer* Player = Screen->GetPlayer();
	const FText Controller = FText::FromString(Screen->GetControllerName());
	const bool bMine = Screen->GetMode() != ETSScreenMode::SyncedMedia && Screen->IsLocallyControlled();

	FText Status;
	FText Times = FText::GetEmpty();
	switch (Screen->GetMode())
	{
	case ETSScreenMode::Browser:
		Status = bMine ? LOCTEXT("StatusMyBrowser", "Showing your browser") : FText::Format(LOCTEXT("StatusBrowser", "Showing {0}'s browser"), Controller);
		break;
	case ETSScreenMode::Window:
		Status = bMine ? LOCTEXT("StatusMyWindow", "Showing your window") : FText::Format(LOCTEXT("StatusWindow", "Showing a window from {0}"), Controller);
		break;
	default:
		Status = Item ? FText::Format(LOCTEXT("NowPlaying", "Now playing: {0}"), FText::FromString(Item->GetDisplayTitle()))
					  : LOCTEXT("NothingPlaying", "Nothing playing. Add a video, open the browser or share a window.");
		if (Item)
		{
			const FText Position = FormatTime(Screen->GetPlaybackPosition());
			Times = Item->bIsLive ? LOCTEXT("Live", "Live")
				: Item->Duration > 0.f ? FText::Format(LOCTEXT("PositionOf", "{0} / {1}"), Position, FormatTime(Item->Duration))
									   : Position;
			if (State.bPaused)
			{
				Times = FText::Format(LOCTEXT("PausedTimes", "{0}  (paused)"), Times);
			}
			if (!Item->AddedBy.IsEmpty())
			{
				Times = FText::Format(LOCTEXT("AddedByTimes", "{0}   added by {1}"), Times, FText::FromString(Item->AddedBy));
			}
		}
		break;
	}
	StatusText->SetText(Status);
	const FText Local = Player ? Player->GetLocalStatus() : FText::GetEmpty();
	if (!Local.IsEmpty())
	{
		Times = Times.IsEmpty() ? Local : FText::Format(LOCTEXT("TimesWithStatus", "{0}   {1}"), Times, Local);
	}
	TimeText->SetText(Times);
	Show(LoadAnywayButton, Player && !Player->GetBlockedReason().IsEmpty());
	RefreshSharing();
}

void UTSScreenPanel::RefreshSharing()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Screen || !BrowserUrlBox)
	{
		return;
	}
	UTSScreenPlayer* Player = Screen->GetPlayer();
	if (Player && !BrowserStateHandle.IsValid())
	{
		BrowserStateHandle = Player->OnBrowserStateChanged.AddUObject(this, &UTSScreenPanel::RefreshSharing);
	}
	const bool bBrowserMode = Screen->GetMode() == ETSScreenMode::Browser;
	const bool bWindowMode = Screen->GetMode() == ETSScreenMode::Window;
	const bool bShared = bBrowserMode || bWindowMode;
	const bool bMine = bShared && Screen->IsLocallyControlled();
	const bool bMyBrowser = bBrowserMode && bMine;
	const bool bHostTools = UTSRemoteCallObject::IsLocalHostOrAdmin(this);
	const FString FocusKey = UTSConfig::GetString(this, UTSConfig::FocusKeyKey, TEXT("F8"));
	const FString CurtainKey = UTSConfig::GetString(this, UTSConfig::CurtainKeyKey, TEXT("F10"));

	// Sharing bar.
	Show(SharingBar, bShared);
	if (bShared)
	{
		const FText Stats = Player ? Player->GetStreamStats() : FText::GetEmpty();
		FText Line;
		if (bMine)
		{
			const int32 Viewers = Screen->GetViewerCount();
			Line = Screen->IsCurtainOn() ? FText::Format(LOCTEXT("CurtainActive", "Blanked: others see \"paused\" while you keep using it. {0} shows it again."),
											   FText::FromString(CurtainKey))
				: Viewers == 0			 ? LOCTEXT("NoViewers", "No one is close enough to watch yet.")
										 : FText::Format(LOCTEXT("StreamingTo", "{0} player(s) watching."), FText::AsNumber(Viewers));
			if (!Stats.IsEmpty() && !Screen->IsCurtainOn())
			{
				Line = FText::Format(LOCTEXT("StreamingStats", "{0}   {1}"), Line, Stats);
			}
		}
		else
		{
			Line = Stats.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("ViewingStats", "Stream: {0}"), Stats);
		}
		SharingText->SetText(Line);
		Show(SharingText, !Line.IsEmpty());
		TSVanillaUI::SetButtonText(OpenWindowButton, FText::Format(LOCTEXT("OpenWindowKey", "Use it in a window ({0})"), FText::FromString(FocusKey)));
		TSVanillaUI::SetButtonText(CurtainButton, Screen->IsCurtainOn() ? LOCTEXT("CurtainOff", "Show it again")
																		 : FText::Format(LOCTEXT("CurtainOnKey", "Blank for everyone ({0})"), FText::FromString(CurtainKey)));
		TSVanillaUI::SetButtonText(StopSharingButton, bWindowMode ? LOCTEXT("WindowStop", "Stop sharing") : LOCTEXT("BrowserStop", "Close the browser"));
	}
	Show(OpenWindowButton, bMine);
	Show(CurtainButton, bMine);
	Show(StopSharingButton, bMine);
	Show(TakeOverButton, bBrowserMode && !bMine);
	Show(HostStopButton, bShared && !bMine && bHostTools);

	// Tabs: what can be done here right now.
	const bool bBrowserAvailable = Host && Host->IsBrowserAvailable();
#if PLATFORM_WINDOWS
	const bool bWindowsAvailable = Host != nullptr;
#else
	const bool bWindowsAvailable = false;
#endif
	if (TabButtons.Num() == 4)
	{
		Show(TabButtons[int32(ETab::Window)], bWindowsAvailable);
	}

	// Browser page.
	Show(BrowserOpenButton, bBrowserAvailable && !bMyBrowser);
	Show(BrowserNavRow, bMyBrowser);
	TSVanillaUI::SetButtonText(BrowserOpenButton, bShared && !bMine ? LOCTEXT("BrowserOpenInstead", "Open my browser here instead") : LOCTEXT("BrowserOpen", "Open the browser"));
	if (bMyBrowser && Player && !TSVanillaUI::InputHasFocus(BrowserUrlBox))
	{
		const FText Url = FText::FromString(Player->GetBrowserState().Url);
		if (!Url.EqualTo(TSVanillaUI::GetInputText(BrowserUrlBox)))
		{
			TSVanillaUI::SetInputText(BrowserUrlBox, Url);
		}
	}
	if (!bBrowserAvailable)
	{
		BrowserNote->SetText(LOCTEXT("BrowserMissing", "The browser component isn't installed on this PC."));
	}
	else if (bMyBrowser)
	{
		BrowserNote->SetText(FText::Format(LOCTEXT("BrowserMine",
			"Type an address and Go, or use the browser in its window ({0}: mouse and keyboard go to it; {0} again or hold Esc returns to the game). "
			"Everyone near the screen sees and hears it."), FText::FromString(FocusKey)));
	}
	else if (bBrowserMode)
	{
		BrowserNote->SetText(FText::Format(LOCTEXT("BrowserOtherNote", "{0} is using this screen's browser. You can open yours here instead if the host allows it."),
			FText::FromString(Screen->GetControllerName())));
	}
	else
	{
		BrowserNote->SetText(LOCTEXT("BrowserNote",
			"A built-in browser on this screen: web pages, videos, web games. You use it in a window; everyone near the screen sees and hears it."));
	}
	UTSScreenPlayer* LoopSource = nullptr;
	for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
	{
		if (*It != Screen && It->IsLocallyControlled() && It->GetPlayer() && It->GetPlayer()->HasBrowser())
		{
			LoopSource = It->GetPlayer();
			break;
		}
	}
	const bool bLooping = Player && Player->IsLoopback();
	Show(LoopbackRow, LoopSource || bLooping);
	LoopbackText->SetText(bLooping ? Player->GetLoopbackStats()
								   : LOCTEXT("LoopbackHint", "Stream test: show your browser from another screen here, encoded and decoded like a live stream."));

	// Window page.
	if (bWindowMode && bMine)
	{
		WindowNote->SetText(LOCTEXT("WindowMine",
			"Others see this window and hear its sound. It can stay behind the game, but not minimised. In its window (button above) clicks and "
			"keys are sent to it; most apps accept them, games and VMs usually don't. Pick another window below to switch."));
	}
	else if (bWindowListPending)
	{
		WindowNote->SetText(LOCTEXT("WindowListing", "Looking for windows..."));
	}
	else if (Windows.Num() == 0)
	{
		WindowNote->SetText(LOCTEXT("NoWindows", "No windows found. Open the program you want to share, then Refresh list."));
	}
	else
	{
		WindowNote->SetText(LOCTEXT("WindowPick", "Pick a window to show on this screen with its sound. Only that window is shared, not your desktop."));
	}
}

void UTSScreenPanel::RequestWindowList()
{
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host)
	{
		return;
	}
	bWindowListPending = true;
	RefreshSharing();
	TWeakObjectPtr<UTSScreenPanel> WeakThis(this);
	const bool bReady = Host->EnsureConnected();
	auto OnList = [WeakThis](const TArray<FTSWindowInfo>& List)
	{
		if (UTSScreenPanel* Panel = WeakThis.Get())
		{
			Panel->bWindowListPending = false;
			Panel->Windows = List;
			Panel->ShowWindowList();
		}
	};
	if (bReady)
	{
		Host->ListWindows(OnList);
		return;
	}
	// The helper is starting: ask again once it is up.
	FTimerHandle Retry;
	GetWorld()->GetTimerManager().SetTimer(Retry, FTimerDelegate::CreateWeakLambda(this, [this, OnList]()
	{
		if (UTSHostSubsystem* Later = UTSHostSubsystem::Get(this))
		{
			Later->ListWindows(OnList);
		}
	}), 1.5f, false);
}

void UTSScreenPanel::ShowWindowList()
{
	WindowListBox->ClearChildren();
	for (int32 I = 0; I < Windows.Num(); ++I)
	{
		const FTSWindowInfo& Info = Windows[I];
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
		Labels->AddChildToVerticalBox(MakeText(FText::FromString(Info.Title), 14));
		if (!Info.Exe.IsEmpty())
		{
			Labels->AddChildToVerticalBox(MakeText(FText::FromString(Info.Exe), 11, Muted));
		}
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Labels);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeButton(LOCTEXT("WindowShare", "Share this window"), ETSPanelAction::WindowShare, I, true))
			->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		WindowListBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f));
	}
	RefreshSharing();
}

void UTSScreenPanel::AskConfirm(EConfirm Kind, int32 Index)
{
	const bool bWindow = Kind == EConfirm::ShareWindow;
	if (UTSConfig::GetBool(this, bWindow ? UTSConfig::WindowDisclaimerKey : UTSConfig::StreamDisclaimerKey, false))
	{
		RunConfirmed(Kind, Index);
		return;
	}
	PendingConfirm = Kind;
	PendingIndex = Index;
	const FText CurtainKey = FText::FromString(UTSConfig::GetString(this, UTSConfig::CurtainKeyKey, TEXT("F10")));
	if (bWindow)
	{
		ConfirmTitle->SetText(LOCTEXT("ConfirmWindowTitle", "Share a window with everyone?"));
		ConfirmText->SetText(FText::Format(LOCTEXT("ConfirmWindow",
			"Everything that appears in the window you share is visible to every player near this screen, and they hear its sound. That includes "
			"notifications and anything you open in it. Only that window is shared, not your desktop.\n\n{0} blanks it for everyone at any time. "
			"This question appears only once."), CurtainKey));
	}
	else
	{
		ConfirmTitle->SetText(LOCTEXT("ConfirmBrowserTitle", "Share your browser with everyone?"));
		ConfirmText->SetText(FText::Format(LOCTEXT("ConfirmBrowser",
			"Everything you show in this browser is visible to every player near this screen, including anything you're signed in to. Don't show "
			"passwords or private messages.\n\n{0} blanks it for everyone at any time. This question appears only once."), CurtainKey));
	}
	Show(ConfirmLayer, true);
}

void UTSScreenPanel::RunConfirmed(EConfirm Kind, int32 Index)
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	if (!Screen || !RCO)
	{
		SetNotice(LOCTEXT("NoRCOConfirm", "Telescreen isn't installed on the server."), true);
		return;
	}
	switch (Kind)
	{
	case EConfirm::OpenBrowser:
	case EConfirm::TakeOver:
		RCO->Server_TakeControl(Screen, ETSScreenMode::Browser, Kind == EConfirm::OpenBrowser ? TSVanillaUI::GetInputText(BrowserUrlBox).ToString().TrimStartAndEnd() : FString());
		OpenWindowWhenMineUntil = FPlatformTime::Seconds() + OpenWindowTimeout;
		SetNotice(LOCTEXT("OpeningBrowser", "Opening the browser..."));
		break;
	case EConfirm::ShareWindow:
		if (Windows.IsValidIndex(Index))
		{
			UE_LOG(LogTelescreen, Log, TEXT("%s: sharing window \"%s\" (%s)"), *Screen->GetName(), *Windows[Index].Title, *Windows[Index].Exe);
			RCO->Server_TakeControl(Screen, ETSScreenMode::Window, FString::Printf(TEXT("%llu"), Windows[Index].Window));
			SetNotice(FText::Format(LOCTEXT("SharingWindow", "Sharing \"{0}\"..."), FText::FromString(Windows[Index].Title)));
		}
		break;
	default:
		break;
	}
}

void UTSScreenPanel::RebuildQueue()
{
	ATSBuildableScreen* Screen = GetScreen();
	QueueBox->ClearChildren();
	if (!Screen)
	{
		return;
	}
	const FTSMediaState& State = Screen->GetMediaState();
	if (State.Queue.Num() == 0)
	{
		QueueBox->AddChildToVerticalBox(MakeText(LOCTEXT("QueueEmpty", "The queue is empty."), 13, Muted));
		return;
	}
	for (int32 I = 0; I < State.Queue.Num(); ++I)
	{
		const FTSMediaItem& Item = State.Queue[I];
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		const bool bCurrent = I == State.CurrentIndex;
		FText Label = FText::Format(LOCTEXT("QueueRow", "{0}. {1}"), FText::AsNumber(I + 1), FText::FromString(Item.GetDisplayTitle()));
		if (Item.Duration > 0.f)
		{
			Label = FText::Format(LOCTEXT("QueueRowTime", "{0}  ({1})"), Label, FormatTime(Item.Duration));
		}
		UTextBlock* Text = MakeText(Label, 13, bCurrent ? Accent : FLinearColor::White);
		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeButton(LOCTEXT("PlayRow", "Play"), ETSPanelAction::PlayIndex, I))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		Row->AddChildToHorizontalBox(MakeButton(LOCTEXT("RemoveRow", "Remove"), ETSPanelAction::RemoveIndex, I))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		QueueBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 2.f));
	}
}

void UTSScreenPanel::SetNotice(const FText& Text, bool bError)
{
	if (NoticeText)
	{
		NoticeText->SetText(Text);
		NoticeText->SetColorAndOpacity(FSlateColor(bError ? ErrorColor : Muted));
	}
}

void UTSScreenPanel::AddLink(bool bPlayNow)
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	const FString Url = TSVanillaUI::GetInputText(LinkBox).ToString().TrimStartAndEnd().TrimQuotes();
	if (!Screen || !RCO || Url.IsEmpty())
	{
		if (!RCO)
		{
			SetNotice(LOCTEXT("NoRCO", "Telescreen isn't installed on the server."), true);
		}
		return;
	}
	FTSMediaItem Item;
	Item.Url = Url;
	if (!Url.Contains(TEXT("://")))
	{
		Item.Title = FPaths::GetCleanFilename(Url);
		RCO->Server_Enqueue(Screen, Item, bPlayNow);
		TSVanillaUI::SetInputText(LinkBox, FText::GetEmpty());
		SetNotice(FText::Format(LOCTEXT("AddedFile", "Added {0}. Other players need the same file at the same path."), FText::FromString(Item.Title)));
		return;
	}
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !Host->EnsureConnected())
	{
		RCO->Server_Enqueue(Screen, Item, bPlayNow);
		TSVanillaUI::SetInputText(LinkBox, FText::GetEmpty());
		SetNotice(LOCTEXT("AddedNoTitle", "Added without a title (Telescreen's player isn't running on this PC)."));
		return;
	}
	SetNotice(LOCTEXT("LookingUp", "Looking up the link..."));
	TWeakObjectPtr<UTSScreenPanel> WeakThis(this);
	TWeakObjectPtr<ATSBuildableScreen> WeakScreen(Screen);
	Host->Probe(Url, [WeakThis, WeakScreen, Item, bPlayNow](const FTSProbeResult& Result) mutable
	{
		UTSScreenPanel* Panel = WeakThis.Get();
		UTSRemoteCallObject* RCO = Panel ? UTSRemoteCallObject::GetLocal(Panel) : nullptr;
		if (!Panel || !WeakScreen.IsValid() || !RCO)
		{
			return;
		}
		if (!Result.bOk)
		{
			Panel->SetNotice(FText::Format(LOCTEXT("ProbeFailed", "Couldn't read that link: {0}"), FText::FromString(Result.Error)), true);
			return;
		}
		Item.Title = Result.Title;
		Item.Duration = float(Result.Duration);
		Item.bIsLive = Result.bIsLive;
		RCO->Server_Enqueue(WeakScreen.Get(), Item, bPlayNow);
		TSVanillaUI::SetInputText(Panel->LinkBox, FText::GetEmpty());
		Panel->SetNotice(FText::Format(LOCTEXT("Added", "Added \"{0}\"."), FText::FromString(Result.Title)));
	});
}

void UTSScreenPanel::HandleAction(ETSPanelAction Action, int32 Index)
{
	ATSBuildableScreen* Screen = GetScreen();
	switch (Action)
	{
	case ETSPanelAction::Close:
		Close();
		return;
	case ETSPanelAction::TogglePip:
		UTSScreenWindow::TogglePinned(Screen);
		Refresh();
		return;
	case ETSPanelAction::Tab:
		SelectTab(ETab(FMath::Clamp(Index, 0, 3)));
		return;
	case ETSPanelAction::ConfirmAccept:
	{
		const EConfirm Kind = PendingConfirm;
		const int32 ConfirmIndex = PendingIndex;
		PendingConfirm = EConfirm::None;
		Show(ConfirmLayer, false);
		UTSConfig::SetBool(this, Kind == EConfirm::ShareWindow ? UTSConfig::WindowDisclaimerKey : UTSConfig::StreamDisclaimerKey, true);
		UE_LOG(LogTelescreen, Log, TEXT("Sharing notice %d accepted"), int32(Kind));
		RunConfirmed(Kind, ConfirmIndex);
		return;
	}
	case ETSPanelAction::ConfirmCancel:
		PendingConfirm = EConfirm::None;
		Show(ConfirmLayer, false);
		return;
	case ETSPanelAction::WindowRefresh:
		RequestWindowList();
		return;
	case ETSPanelAction::OpenWindow:
		Close();
		UTSScreenWindow::OpenInteractive(Screen);
		return;
	default:
		break;
	}
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	if (!Screen || !RCO)
	{
		SetNotice(LOCTEXT("NoRCOAction", "Telescreen isn't installed on the server."), true);
		return;
	}
	const FTSMediaState& State = Screen->GetMediaState();
	const double Position = Screen->GetPlaybackPosition();
	switch (Action)
	{
	case ETSPanelAction::PlayNow: AddLink(true); break;
	case ETSPanelAction::AddToQueue: AddLink(false); break;
	case ETSPanelAction::PlayPause:
		if (!State.GetCurrent() && State.Queue.Num() > 0)
		{
			RCO->Server_MediaCommand(Screen, ETSMediaCommand::PlayIndex, 0, 0.0);
		}
		else
		{
			RCO->Server_MediaCommand(Screen, State.bPaused ? ETSMediaCommand::Resume : ETSMediaCommand::Pause, 0, 0.0);
		}
		break;
	case ETSPanelAction::Next: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Next, 0, 0.0); break;
	case ETSPanelAction::Stop: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Stop, 0, 0.0); break;
	case ETSPanelAction::Back10: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Seek, 0, FMath::Max(0.0, Position - 10.0)); break;
	case ETSPanelAction::Forward10: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Seek, 0, Position + 10.0); break;
	case ETSPanelAction::PlayIndex: RCO->Server_MediaCommand(Screen, ETSMediaCommand::PlayIndex, Index, 0.0); break;
	case ETSPanelAction::RemoveIndex: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Remove, Index, 0.0); break;
	case ETSPanelAction::ClearQueue: RCO->Server_MediaCommand(Screen, ETSMediaCommand::Clear, 0, 0.0); break;
	case ETSPanelAction::SaveName:
		RCO->Server_SetScreenSettings(Screen, TSVanillaUI::GetInputText(NameBox).ToString().Left(64), Screen->GetAspectMode());
		SetNotice(LOCTEXT("NameSaved", "Name saved."));
		break;
	case ETSPanelAction::AspectLetterbox: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Letterbox); break;
	case ETSPanelAction::AspectFill: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Fill); break;
	case ETSPanelAction::AspectStretch: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Stretch); break;
	case ETSPanelAction::BrowserOpen: AskConfirm(EConfirm::OpenBrowser, INDEX_NONE); break;
	case ETSPanelAction::BrowserTakeOver: AskConfirm(EConfirm::TakeOver, INDEX_NONE); break;
	case ETSPanelAction::WindowShare: AskConfirm(EConfirm::ShareWindow, Index); break;
	case ETSPanelAction::CurtainToggle: RCO->Server_SetCurtain(Screen, !Screen->IsCurtainOn()); break;
	case ETSPanelAction::StopSharing:
	case ETSPanelAction::HostStop:
		RCO->Server_ReleaseControl(Screen);
		break;
	case ETSPanelAction::BrowserGo:
		// Enter in the address box: navigates the player's own browser, or opens one.
		if (UTSScreenPlayer* Player = Screen->GetPlayer(); Player && Player->HasBrowser())
		{
			Player->BrowserNavigate(TSVanillaUI::GetInputText(BrowserUrlBox).ToString());
		}
		else
		{
			AskConfirm(EConfirm::OpenBrowser, INDEX_NONE);
		}
		break;
	case ETSPanelAction::BrowserBack:
	case ETSPanelAction::BrowserForward:
	case ETSPanelAction::BrowserReload:
		if (UTSScreenPlayer* Player = Screen->GetPlayer())
		{
			Player->BrowserCommand(Action == ETSPanelAction::BrowserBack ? TSHostProtocol::ECommand::Back
				: Action == ETSPanelAction::BrowserForward				? TSHostProtocol::ECommand::Forward
																		: TSHostProtocol::ECommand::Reload);
		}
		break;
	case ETSPanelAction::LoopbackToggle:
		if (UTSScreenPlayer* Player = Screen->GetPlayer())
		{
			if (Player->IsLoopback())
			{
				Player->StopLoopback();
			}
			else
			{
				for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
				{
					if (*It != Screen && It->IsLocallyControlled() && It->GetPlayer() && It->GetPlayer()->HasBrowser())
					{
						Player->StartLoopback(It->GetPlayer());
						break;
					}
				}
			}
		}
		Refresh();
		break;
	case ETSPanelAction::LoadAnyway:
		if (UTSScreenPlayer* Player = Screen->GetPlayer())
		{
			Player->LoadAnyway();
		}
		break;
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
