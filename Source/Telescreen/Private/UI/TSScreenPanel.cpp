#include "UI/TSScreenPanel.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Screen/TSScreenVolumes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "EngineUtils.h"
#include "FGHUD.h"
#include "GameFramework/PlayerController.h"
#include "Screen/TSLocalVideos.h"
#include "Screen/TSRemoteConnections.h"
#include "Screen/TSScreenPlayer.h"
#include "Components/EditableTextBox.h"
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

UWidget* UTSScreenPanel::MakeListButton(const FText& Title, const FText& Description, bool bSelected, ETSPanelAction Action, int32 Index)
{
	return TSVanillaUI::MakeListButton(WidgetTree, Title, Description, bSelected, MakeHandler(Action, Index), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleClicked));
}

UWidget* UTSScreenPanel::MakeInput(const FText& Hint, ETSPanelAction OnEnter)
{
	return TSVanillaUI::MakeInputBox(WidgetTree, Hint, MakeHandler(OnEnter, INDEX_NONE), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleCommitted));
}

void UTSScreenPanel::OnWindowClosed()
{
	Close();
}

void UTSScreenPanel::OnSeekBegin()
{
	bSeeking = true;
}

void UTSScreenPanel::OnSeekChanged(float Value)
{
	const ATSBuildableScreen* Screen = GetScreen();
	const FTSMediaItem* Item = Screen ? Screen->GetMediaState().GetCurrent() : nullptr;
	if (bSeeking && Item && Item->Duration > 0.f)
	{
		SeekPositionText->SetText(FormatTime(Value * Item->Duration));
	}
}

void UTSScreenPanel::OnSeekEnd()
{
	bSeeking = false;
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	const FTSMediaItem* Item = Screen ? Screen->GetMediaState().GetCurrent() : nullptr;
	if (RCO && Item && Item->Duration > 0.f && !Item->bIsLive)
	{
		RCO->Server_MediaCommand(Screen, ETSMediaCommand::Seek, 0, double(SeekSlider->GetValue()) * Item->Duration);
	}
}

void UTSScreenPanel::OnVolumeChanged(float Value)
{
	ATSBuildableScreen* Screen = GetScreen();
	TSScreenVolumes::Set(Screen, Value, false);
	VolumeText->SetText(FText::Format(LOCTEXT("VolumePercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value * 100.f))));
	if (UTSScreenPlayer* Player = Screen ? Screen->GetPlayer() : nullptr)
	{
		Player->RefreshVolume();
	}
}

void UTSScreenPanel::RebuildSubtitles()
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSScreenPlayer* Player = Screen ? Screen->GetPlayer() : nullptr;
	const int32 Mode = int32(UTSScreenPlayer::GetSubtitleMode(this));
	for (int32 I = 0; I < SubtitleModeButtons.Num(); ++I)
	{
		TSVanillaUI::SetTabActive(SubtitleModeButtons[I], I == Mode);
	}
	// Rebuilt only when the tracks or the selection change.
	FString Signature = FString::Printf(TEXT("%d|%u|"), Mode, Player ? Player->GetSelectedSubtitleTrack() : 0);
	if (Player)
	{
		for (const FTSSubtitleTrack& Track : Player->GetSubtitleTracks())
		{
			Signature += FString::Printf(TEXT("%u:%s;"), Track.Id, *Track.Label);
		}
	}
	if (Signature == ShownSubtitles)
	{
		return;
	}
	ShownSubtitles = Signature;
	SubtitleTracksBox->ClearChildren();
	if (!Player || Player->GetSubtitleTracks().Num() == 0)
	{
		SubtitleTracksBox->AddChildToWrapBox(MakeText(Player && Player->IsReceivingStream()
			? LOCTEXT("SubtitlesStreamed", "No subtitles for a video streamed from another player.")
			: LOCTEXT("SubtitlesNone", "This video has no subtitles (files: text tracks inside it, or a .srt / .vtt with the same name next to it)."), 12, Muted));
		return;
	}
	if (Mode == 0)
	{
		SubtitleTracksBox->AddChildToWrapBox(MakeText(LOCTEXT("SubtitlesPickMode", "Pick where to show them; then a track:"), 12, Muted));
	}
	else if (Mode == 2 && Player->GetSubtitleTracks().ContainsByPredicate([](const FTSSubtitleTrack& Track) { return Track.Kind == 4; }))
	{
		SubtitleTracksBox->AddChildToWrapBox(MakeText(LOCTEXT("SubtitlesPictureHud", "Picture tracks show on the screen only:"), 12, Muted));
	}
	for (const FTSSubtitleTrack& Track : Player->GetSubtitleTracks())
	{
		const FText Kind = Track.Kind == 1 ? LOCTEXT("TrackAuto", " (automatic)") : Track.Kind == 3 ? LOCTEXT("TrackFile", " (file)")
						 : Track.Kind == 4										  ? LOCTEXT("TrackPicture", " (picture)")
																				  : FText::GetEmpty();
		UWidget* Button = MakeButton(FText::Format(LOCTEXT("TrackButton", "{0}{1}"), FText::FromString(Track.Label.Left(40)), Kind),
			ETSPanelAction::SubtitleTrack, int32(Track.Id));
		TSVanillaUI::SetTabActive(Button, Track.Id == Player->GetSelectedSubtitleTrack());
		SubtitleTracksBox->AddChildToWrapBox(Button);
	}
}

void UTSScreenPanel::RefreshVideosFolder()
{
	const FString Folder = TSLocalVideos::GetFolder(this);
	VideosFolderText->SetText(Folder.IsEmpty()
		? LOCTEXT("NoVideosFolder", "Videos folder: not set. Files others add are found by name in it.")
		: FText::Format(LOCTEXT("VideosFolderIs", "Videos folder: {0}"), FText::FromString(Folder)));
}

void UTSScreenPanel::OnScreenVolumeBegin()
{
	bScreenVolumeDragging = true;
}

void UTSScreenPanel::OnScreenVolumeChanged(float Value)
{
	ScreenVolumeText->SetText(FText::Format(LOCTEXT("VolumePercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value * 100.f))));
}

void UTSScreenPanel::OnScreenVolumeEnd()
{
	bScreenVolumeDragging = false;
	ATSBuildableScreen* Screen = GetScreen();
	if (UTSRemoteCallObject* RCO = Screen ? UTSRemoteCallObject::GetLocal(this) : nullptr)
	{
		RCO->Server_SetScreenVolume(Screen, ScreenVolumeSlider->GetValue());
	}
}

void UTSScreenPanel::OnVolumeEnd()
{
	TSScreenVolumes::Set(GetScreen(), VolumeSlider->GetValue(), true);
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
	auto Heading = [this](const FText& Text) { return MakeText(Text, 14, Accent, true); };
	auto MakeSlider = [this](float Min, float Max)
	{
		USlider* Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->SetMinValue(Min);
		Slider->SetMaxValue(Max);
		Slider->SetSliderBarColor(FLinearColor(0.2f, 0.22f, 0.26f));
		Slider->SetSliderHandleColor(Accent);
		return Slider;
	};

	// The vanilla window frame (title = screen name, header tabs, close button) around the content background; the
	// confirm dialog overlays the content. Sizes follow the viewport (NativeConstruct).
	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	UHorizontalBox* Columns = MakeHBox();
	Layers->AddChildToOverlay(Columns);
	PanelSize = WidgetTree->ConstructWidget<USizeBox>();
	PanelSize->SetWidthOverride(1280.f);
	Window = TSVanillaUI::MakeWindow(WidgetTree, LOCTEXT("PanelTitle", "Telescreen"), TSVanillaUI::MakeContentBackground(WidgetTree, Layers), this,
		GET_FUNCTION_NAME_CHECKED(UTSScreenPanel, OnWindowClosed));
	PanelSize->AddChild(Window);
	WidgetTree->RootWidget = PanelSize;

	// Left column: the screen's picture, what it shows, my volume, and the sharing card.
	UVerticalBox* Left = MakeVBox();
	USizeBox* LeftSize = WidgetTree->ConstructWidget<USizeBox>();
	LeftSize->SetWidthOverride(LeftColumnWidth);
	LeftSize->AddChild(Left);
	UHorizontalBoxSlot* LeftSlot = Columns->AddChildToHorizontalBox(LeftSize);
	LeftSlot->SetPadding(FMargin(0.f, 0.f, 18.f, 0.f));
	LeftSlot->SetVerticalAlignment(VAlign_Top);

	PreviewSize = WidgetTree->ConstructWidget<USizeBox>();
	PreviewSize->SetWidthOverride(LeftColumnWidth);
	PreviewSize->SetHeightOverride(LeftColumnWidth * 9.f / 16.f);
	UBorder* PreviewFrame = WidgetTree->ConstructWidget<UBorder>();
	PreviewFrame->SetBrushColor(FLinearColor::Black);
	PreviewFrame->SetPadding(FMargin(0.f));
	PreviewImage = WidgetTree->ConstructWidget<UImage>();
	PreviewFrame->AddChild(PreviewImage);
	PreviewSize->AddChild(PreviewFrame);
	AddRow(Left, PreviewSize, 0.f);

	StatusText = MakeText(FText::GetEmpty(), 16, FLinearColor::White, true);
	AddRow(Left, StatusText, 10.f);
	TimeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Left, TimeText, 2.f);

	// Label, slider, percentage.
	auto AddVolumeRow = [&](const FText& Label, USlider* Slider, TObjectPtr<UTextBlock>& OutText, float Top)
	{
		UHorizontalBox* Row = MakeHBox();
		UTextBlock* LabelText = MakeText(Label, 13, Muted);
		LabelText->SetAutoWrapText(false);
		USizeBox* LabelSize = WidgetTree->ConstructWidget<USizeBox>();
		LabelSize->SetWidthOverride(150.f);
		LabelSize->AddChild(LabelText);
		AddToHBox(Row, LabelSize);
		AddToHBox(Row, Slider, true);
		OutText = MakeText(FText::GetEmpty(), 13);
		OutText->SetAutoWrapText(false);
		USizeBox* TextSize = WidgetTree->ConstructWidget<USizeBox>();
		TextSize->SetWidthOverride(44.f);
		TextSize->AddChild(OutText);
		AddToHBox(Row, TextSize);
		AddRow(Left, Row, Top);
	};
	VolumeSlider = MakeSlider(0.f, 2.f);
	VolumeSlider->SetStepSize(0.05f);
	VolumeSlider->OnValueChanged.AddDynamic(this, &UTSScreenPanel::OnVolumeChanged);
	VolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnVolumeEnd);
	VolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnVolumeEnd);
	AddVolumeRow(LOCTEXT("MyVolume", "My volume"), VolumeSlider, VolumeText, 10.f);
	ScreenVolumeSlider = MakeSlider(0.f, 1.f);
	ScreenVolumeSlider->SetStepSize(0.05f);
	ScreenVolumeSlider->OnMouseCaptureBegin.AddDynamic(this, &UTSScreenPanel::OnScreenVolumeBegin);
	ScreenVolumeSlider->OnControllerCaptureBegin.AddDynamic(this, &UTSScreenPanel::OnScreenVolumeBegin);
	ScreenVolumeSlider->OnValueChanged.AddDynamic(this, &UTSScreenPanel::OnScreenVolumeChanged);
	ScreenVolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnScreenVolumeEnd);
	ScreenVolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnScreenVolumeEnd);
	AddVolumeRow(LOCTEXT("ScreenVolume", "Screen (everyone)"), ScreenVolumeSlider, ScreenVolumeText, 4.f);
	UHorizontalBox* ScreenButtons = MakeHBox();
	PipButton = MakeButton(LOCTEXT("ShowOnHud", "Show on HUD"), ETSPanelAction::TogglePip);
	AddToHBox(ScreenButtons, PipButton, true);
	SettingsButton = MakeButton(LOCTEXT("ScreenSettings", "Screen settings"), ETSPanelAction::Tab, int32(ETab::Screen));
	UHorizontalBoxSlot* SettingsSlot = ScreenButtons->AddChildToHorizontalBox(SettingsButton);
	SettingsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SettingsSlot->SetVerticalAlignment(VAlign_Center);
	AddRow(Left, ScreenButtons, 8.f);

	// Sharing card: the sharer's controls, or what a viewer can do.
	UVerticalBox* Sharing = MakeVBox();
	AddRow(Sharing, TSVanillaUI::MakeDivider(WidgetTree), 0.f);
	AddRow(Sharing, Heading(LOCTEXT("SharingHeader", "Sharing")), 10.f);
	SharingText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Sharing, SharingText, 4.f);
	OpenWindowButton = MakeButton(LOCTEXT("OpenWindow", "Use it in a window"), ETSPanelAction::OpenWindow, INDEX_NONE, true);
	AddRow(Sharing, OpenWindowButton, 8.f);
	CurtainButton = MakeButton(LOCTEXT("CurtainOn", "Blank for everyone"), ETSPanelAction::CurtainToggle);
	AddRow(Sharing, CurtainButton, 4.f);
	TakeOverButton = MakeButton(LOCTEXT("TakeOver", "Use the browser myself"), ETSPanelAction::BrowserTakeOver);
	AddRow(Sharing, TakeOverButton, 4.f);
	StopSharingButton = MakeButton(LOCTEXT("StopSharing", "Stop"), ETSPanelAction::StopSharing);
	AddRow(Sharing, StopSharingButton, 12.f);
	HostStopButton = MakeButton(LOCTEXT("HostStop", "Stop it (world host)"), ETSPanelAction::HostStop);
	AddRow(Sharing, HostStopButton, 12.f);
	ViewersHeader = Heading(LOCTEXT("ViewersHeader", "Viewers"));
	AddRow(Sharing, ViewersHeader, 12.f);
	ViewersBox = MakeVBox();
	AddRow(Sharing, ViewersBox, 4.f);
	ControlLogHeader = Heading(LOCTEXT("ControlLogHeader", "Control log"));
	AddRow(Sharing, ControlLogHeader, 12.f);
	ControlLogText = MakeText(FText::GetEmpty(), 12, Muted);
	AddRow(Sharing, ControlLogText, 4.f);
	SharingBar = Sharing;
	AddRow(Left, Sharing, 14.f);

	// Right column: the mode tabs, notices, the selected page (or the screen's settings).
	UVerticalBox* Right = MakeVBox();
	UHorizontalBoxSlot* RightSlot = Columns->AddChildToHorizontalBox(Right);
	RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	const FText TabNames[] = {LOCTEXT("TabVideos", "Videos"), LOCTEXT("TabBrowser", "Browser"), LOCTEXT("TabWindow", "Share a window"),
		LOCTEXT("TabRemote", "Remote desktop")};
	UHorizontalBox* Tabs = MakeHBox();
	for (int32 I = 0; I < UE_ARRAY_COUNT(TabNames); ++I)
	{
		UWidget* Tab = TSVanillaUI::MakeTab(WidgetTree, TabNames[I], MakeHandler(ETSPanelAction::Tab, I), GET_FUNCTION_NAME_CHECKED(UTSPanelAction, HandleClicked));
		TabButtons.Add(Tab);
		AddToHBox(Tabs, Tab);
	}
	AddRow(Right, Tabs, 0.f);
	NoticeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Right, NoticeText, 0.f);

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	PagesSize = WidgetTree->ConstructWidget<USizeBox>();
	PagesSize->SetMaxDesiredHeight(620.f);
	PagesSize->AddChild(TSVanillaUI::MakeScrollBox(WidgetTree, Pages));
	AddRow(Right, PagesSize, 6.f);

	// Videos.
	{
		UVerticalBox* Page = MakeVBox();
		AddRow(Page, Heading(LOCTEXT("PlaybackHeader", "Playback")), 0.f);
		UHorizontalBox* Transport = MakeHBox();
		AddToHBox(Transport, MakeButton(LOCTEXT("PlayPause", "Play / Pause"), ETSPanelAction::PlayPause, INDEX_NONE, true));
		AddToHBox(Transport, MakeButton(LOCTEXT("Next", "Next"), ETSPanelAction::Next));
		AddToHBox(Transport, MakeButton(LOCTEXT("Stop", "Stop"), ETSPanelAction::Stop));
		AddRow(Page, Transport, 6.f);
		UHorizontalBox* Seek = MakeHBox();
		SeekPositionText = MakeText(FText::FromString(TEXT("0:00")), 13, Muted);
		SeekPositionText->SetAutoWrapText(false);
		AddToHBox(Seek, SeekPositionText);
		SeekSlider = MakeSlider(0.f, 1.f);
		SeekSlider->OnMouseCaptureBegin.AddDynamic(this, &UTSScreenPanel::OnSeekBegin);
		SeekSlider->OnControllerCaptureBegin.AddDynamic(this, &UTSScreenPanel::OnSeekBegin);
		SeekSlider->OnValueChanged.AddDynamic(this, &UTSScreenPanel::OnSeekChanged);
		SeekSlider->OnMouseCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnSeekEnd);
		SeekSlider->OnControllerCaptureEnd.AddDynamic(this, &UTSScreenPanel::OnSeekEnd);
		AddToHBox(Seek, SeekSlider, true);
		SeekDurationText = MakeText(FText::FromString(TEXT("0:00")), 13, Muted);
		SeekDurationText->SetAutoWrapText(false);
		AddToHBox(Seek, SeekDurationText);
		SeekRow = Seek;
		AddRow(Page, Seek, 8.f);
		UHorizontalBox* SubtitleRow = MakeHBox();
		AddToHBox(SubtitleRow, MakeText(LOCTEXT("SubtitlesLabel", "Subtitles"), 13, Muted));
		const FText ModeNames[] = {LOCTEXT("SubtitlesOff", "Off"), LOCTEXT("SubtitlesScreen", "On screen"), LOCTEXT("SubtitlesHud", "On HUD"),
			LOCTEXT("SubtitlesBoth", "Both")};
		for (int32 I = 0; I < UE_ARRAY_COUNT(ModeNames); ++I)
		{
			UWidget* Button = MakeButton(ModeNames[I], ETSPanelAction::SubtitleMode, I);
			SubtitleModeButtons.Add(Button);
			AddToHBox(SubtitleRow, Button);
		}
		AddRow(Page, SubtitleRow, 8.f);
		SubtitleTracksBox = WidgetTree->ConstructWidget<UWrapBox>();
		SubtitleTracksBox->SetInnerSlotPadding(FVector2D(6.f, 4.f));
		AddRow(Page, SubtitleTracksBox, 4.f);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 14.f);
		AddRow(Page, Heading(LOCTEXT("AddHeader", "Add a video")), 10.f);
		UHorizontalBox* AddBox = MakeHBox();
		LinkBox = MakeInput(LOCTEXT("LinkHint", "Paste a YouTube, Twitch or Vimeo link, or a video file path"), ETSPanelAction::PlayNow);
		AddToHBox(AddBox, LinkBox, true);
		AddToHBox(AddBox, MakeButton(LOCTEXT("PlayNow", "Play now"), ETSPanelAction::PlayNow, INDEX_NONE, true));
		AddToHBox(AddBox, MakeButton(LOCTEXT("AddToQueue", "Add to queue"), ETSPanelAction::AddToQueue));
		AddRow(Page, AddBox);
		// Files: picked from this PC; others play their own copy (same path, or same name in their videos folder),
		// or watch it streamed.
		UHorizontalBox* FolderRow = MakeHBox();
		AddToHBox(FolderRow, MakeButton(LOCTEXT("PickFile", "Pick a file"), ETSPanelAction::PickFile));
		VideosFolderText = MakeText(FText::GetEmpty(), 12, Muted);
		AddToHBox(FolderRow, VideosFolderText, true);
		AddToHBox(FolderRow, MakeButton(LOCTEXT("ChangeVideosFolder", "Change folder"), ETSPanelAction::ChangeVideosFolder));
		AddToHBox(FolderRow, MakeButton(LOCTEXT("OpenVideosFolder", "Open folder"), ETSPanelAction::OpenVideosFolder));
		AddRow(Page, FolderRow, 6.f);
		LoadAnywayButton = MakeButton(LOCTEXT("LoadAnyway", "Load it anyway on this PC"), ETSPanelAction::LoadAnyway);
		Show(LoadAnywayButton, false);
		AddRow(Page, LoadAnywayButton, 4.f);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 14.f);
		UHorizontalBox* QueueHeader = MakeHBox();
		AddToHBox(QueueHeader, Heading(LOCTEXT("QueueHeader", "Queue")), true);
		AddToHBox(QueueHeader, MakeButton(LOCTEXT("ClearQueue", "Clear"), ETSPanelAction::ClearQueue));
		AddRow(Page, QueueHeader, 10.f);
		QueueBox = MakeVBox();
		AddRow(Page, QueueBox, 4.f);
		Pages->AddChild(Page);
	}

	// Browser.
	{
		UVerticalBox* Page = MakeVBox();
		AddRow(Page, Heading(LOCTEXT("BrowserHeader", "Built-in browser")), 0.f);
		BrowserNote = MakeText(FText::GetEmpty(), 13, Muted);
		AddRow(Page, BrowserNote, 4.f);
		UHorizontalBox* UrlRow = MakeHBox();
		BrowserUrlBox = MakeInput(LOCTEXT("BrowserUrlHint", "Web address or search, or leave empty for your start page"), ETSPanelAction::BrowserGo);
		AddToHBox(UrlRow, BrowserUrlBox, true);
		BrowserOpenButton = MakeButton(LOCTEXT("BrowserOpen", "Open the browser"), ETSPanelAction::BrowserOpen, INDEX_NONE, true);
		AddToHBox(UrlRow, BrowserOpenButton);
		AddRow(Page, UrlRow, 10.f);
		UHorizontalBox* NavRow = MakeHBox();
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserGo", "Go"), ETSPanelAction::BrowserGo, INDEX_NONE, true));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserBack", "Back"), ETSPanelAction::BrowserBack));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserForward", "Forward"), ETSPanelAction::BrowserForward));
		AddToHBox(NavRow, MakeButton(LOCTEXT("BrowserReload", "Reload"), ETSPanelAction::BrowserReload));
		BrowserNavRow = NavRow;
		AddRow(Page, NavRow, 6.f);
		UHorizontalBox* Loopback = MakeHBox();
		LoopbackText = MakeText(FText::GetEmpty(), 12, Muted);
		AddToHBox(Loopback, LoopbackText, true);
		AddToHBox(Loopback, MakeButton(LOCTEXT("LoopbackToggle", "Stream test"), ETSPanelAction::LoopbackToggle));
		LoopbackRow = Loopback;
		AddRow(Page, Loopback, 14.f);
		Pages->AddChild(Page);
	}

	// Share a window.
	{
		UVerticalBox* Page = MakeVBox();
		UHorizontalBox* Top = MakeHBox();
		AddToHBox(Top, Heading(LOCTEXT("WindowHeader", "Share a window from your PC")), true);
		AddToHBox(Top, MakeButton(LOCTEXT("WindowRefresh", "Refresh list"), ETSPanelAction::WindowRefresh));
		AddRow(Page, Top, 0.f);
		WindowNote = MakeText(FText::GetEmpty(), 13, Muted);
		AddRow(Page, WindowNote, 4.f);
		WindowListBox = MakeVBox();
		AddRow(Page, WindowListBox, 8.f);
		Pages->AddChild(Page);
	}

	// Remote desktop.
	{
		UVerticalBox* Page = MakeVBox();
		AddRow(Page, Heading(LOCTEXT("RemoteHeader", "Remote desktop (VNC)")), 0.f);
		RemoteNote = MakeText(FText::GetEmpty(), 13, Muted);
		AddRow(Page, RemoteNote, 4.f);
		UHorizontalBox* ConnectRow = MakeHBox();
		RemoteAddressBox = MakeInput(LOCTEXT("RemoteAddressHint", "VNC address, e.g. 192.168.1.20:5901 or myvm:1"), ETSPanelAction::RemoteConnect);
		AddToHBox(ConnectRow, RemoteAddressBox, true);
		RemoteUserBox = MakeInput(LOCTEXT("RemoteUserHint", "User (if asked)"), ETSPanelAction::RemoteConnect);
		USizeBox* UserSize = WidgetTree->ConstructWidget<USizeBox>();
		UserSize->SetWidthOverride(150.f);
		UserSize->AddChild(RemoteUserBox);
		AddToHBox(ConnectRow, UserSize);
		RemotePasswordBox = MakeInput(LOCTEXT("RemotePasswordHint", "Password, if any"), ETSPanelAction::RemoteConnect);
		TSVanillaUI::SetInputPassword(RemotePasswordBox, true);
		USizeBox* PasswordSize = WidgetTree->ConstructWidget<USizeBox>();
		PasswordSize->SetWidthOverride(200.f);
		PasswordSize->AddChild(RemotePasswordBox);
		AddToHBox(ConnectRow, PasswordSize);
		AddRow(Page, ConnectRow, 10.f);
		UHorizontalBox* SecurityRow = MakeHBox();
		RemoteSecurityText = MakeText(FText::GetEmpty(), 12, Muted);
		AddToHBox(SecurityRow, RemoteSecurityText, true);
		TrustCertificateButton = MakeButton(LOCTEXT("TrustCertificate", "Trust the new certificate"), ETSPanelAction::RemoteTrustCertificate);
		AddToHBox(SecurityRow, TrustCertificateButton);
		AddRow(Page, SecurityRow, 6.f);
		UHorizontalBox* ConnectButtons = MakeHBox();
		AddToHBox(ConnectButtons, MakeButton(LOCTEXT("RemoteConnect", "Connect"), ETSPanelAction::RemoteConnect, INDEX_NONE, true));
		AddToHBox(ConnectButtons, MakeButton(LOCTEXT("RemoteSave", "Save"), ETSPanelAction::RemoteSave));
		AddRow(Page, ConnectButtons, 6.f);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 14.f);
		AddRow(Page, Heading(LOCTEXT("RemoteSavedHeader", "Saved connections (this PC only)")), 10.f);
		RemoteListBox = MakeVBox();
		AddRow(Page, RemoteListBox, 4.f);
		Pages->AddChild(Page);
	}

	// Screen.
	{
		UVerticalBox* Page = MakeVBox();
		AddRow(Page, Heading(LOCTEXT("SettingsPageHeader", "Screen settings")), 0.f);
		AddRow(Page, MakeText(LOCTEXT("SettingsPageNote", "For this screen, for everyone. Pick a mode above to go back."), 13, Muted), 4.f);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 12.f);
		AddRow(Page, Heading(LOCTEXT("NameHeader", "Name")), 10.f);
		UHorizontalBox* NameRow = MakeHBox();
		NameBox = MakeInput(LOCTEXT("NameHint", "Screen name, e.g. Main Hall"), ETSPanelAction::SaveName);
		AddToHBox(NameRow, NameBox, true);
		AddToHBox(NameRow, MakeButton(LOCTEXT("SaveName", "Save name"), ETSPanelAction::SaveName));
		AddRow(Page, NameRow);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 14.f);
		AddRow(Page, Heading(LOCTEXT("FitHeader", "Picture fit (videos and shared windows)")), 10.f);
		AspectText = MakeText(LOCTEXT("FitNote", "Letterbox shows the whole picture with bars, Fill crops it to the screen, Stretch "
			"distorts it to the screen. Shared browsers always match the screen."), 13, Muted);
		AddRow(Page, AspectText, 4.f);
		UHorizontalBox* AspectRow = MakeHBox();
		AspectButtons.Add(MakeButton(LOCTEXT("Letterbox", "Letterbox"), ETSPanelAction::AspectLetterbox));
		AspectButtons.Add(MakeButton(LOCTEXT("Fill", "Fill"), ETSPanelAction::AspectFill));
		AspectButtons.Add(MakeButton(LOCTEXT("Stretch", "Stretch"), ETSPanelAction::AspectStretch));
		for (UWidget* Button : AspectButtons)
		{
			AddToHBox(AspectRow, Button);
		}
		AddRow(Page, AspectRow);
		AddRow(Page, TSVanillaUI::MakeDivider(WidgetTree), 14.f);
		AddRow(Page, Heading(LOCTEXT("SettingsHeader", "More settings")), 10.f);
		AddRow(Page, MakeText(LOCTEXT("SettingsNote",
			"Stream resolution, bitrate, frame rate and encoder, browser page size, keys and privacy are in Mods > Telescreen. The world host's rules "
			"(who may share, stream limits) are there too, under \"Rules for this world\"."), 13, Muted), 4.f);
		Pages->AddChild(Page);
	}

	// Confirm dialog over the whole panel.
	UBorder* Shade = WidgetTree->ConstructWidget<UBorder>();
	Shade->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Shade->SetHorizontalAlignment(HAlign_Center);
	Shade->SetVerticalAlignment(VAlign_Center);
	USizeBox* DialogSize = WidgetTree->ConstructWidget<USizeBox>();
	DialogSize->SetWidthOverride(600.f);
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

void UTSScreenPanel::FitToViewport()
{
	// About two thirds of the viewport wide (1100-1440) and up to 80% tall, in UI units.
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.1f);
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this) / Scale;
	if (Viewport.X < 100.0 || Viewport.Y < 100.0)
	{
		return;
	}
	const float Width = FMath::Clamp(float(Viewport.X) * 0.67f, 1100.f, FMath::Max(1100.f, FMath::Min(1440.f, float(Viewport.X) - 40.f)));
	PanelSize->SetWidthOverride(Width);
	PagesSize->SetMaxDesiredHeight(FMath::Clamp(float(Viewport.Y) * 0.8f - 190.f, 320.f, 900.f));
	UE_LOG(LogTelescreen, Verbose, TEXT("Screen panel: %.0f wide for a %.0fx%.0f viewport"), Width, Viewport.X, Viewport.Y);
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
		const float MyVolume = TSScreenVolumes::Get(Screen);
		VolumeSlider->SetValue(MyVolume);
		VolumeText->SetText(FText::Format(LOCTEXT("VolumePercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(MyVolume * 100.f))));
	}
	NoticeHandle = UTSRemoteCallObject::OnNotice.AddWeakLambda(this, [this](const FText& Text) { SetNotice(Text, true); });
	FitToViewport();
	RefreshVideosFolder();
	// Masking again once the input box exists (its construction may reset it).
	TSVanillaUI::SetInputPassword(RemotePasswordBox, true);
	// Open on the tab for what the screen does now.
	const ETSScreenMode Mode = Screen ? Screen->GetMode() : ETSScreenMode::SyncedMedia;
	SelectTab(Mode == ETSScreenMode::Browser		 ? ETab::Browser
			  : Mode == ETSScreenMode::Window		 ? ETab::Window
			  : Mode == ETSScreenMode::RemoteDesktop ? ETab::RemoteDesktop
													 : ETab::Videos);
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
			Player->OnSubtitlesChanged.Remove(SubtitlesHandle);
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
	TSVanillaUI::SetTabActive(SettingsButton, Tab == ETab::Screen);
	for (int32 I = 0; I < TabButtons.Num(); ++I)
	{
		TSVanillaUI::SetTabActive(TabButtons[I], I == int32(Tab));
	}
	if (Tab == ETab::Window && Windows.Num() == 0)
	{
		RequestWindowList();
	}
	if (Tab == ETab::RemoteDesktop && !bRemoteListLoaded)
	{
		bRemoteListLoaded = true;
		SavedRemotes = TSRemoteConnections::LoadSaved();
		ShowSavedRemotes();
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
	for (int32 I = 0; I < AspectButtons.Num(); ++I)
	{
		TSVanillaUI::SetTabActive(AspectButtons[I], I == int32(Screen->GetAspectMode()));
	}
	if (!bScreenVolumeDragging)
	{
		ScreenVolumeSlider->SetValue(Screen->GetScreenVolume());
		ScreenVolumeText->SetText(FText::Format(LOCTEXT("VolumePercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Screen->GetScreenVolume() * 100.f))));
	}
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
	case ETSScreenMode::RemoteDesktop:
		Status = bMine ? LOCTEXT("StatusMyRemote", "Showing your remote desktop") : FText::Format(LOCTEXT("StatusRemote", "Showing a remote desktop from {0}"), Controller);
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
	UTexture* Picture = Player ? Player->GetDisplayTexture() : nullptr;
	if (Picture != ShownPreview)
	{
		ShownPreview = Picture;
		PreviewImage->SetBrushResourceObject(Picture);
		PreviewImage->SetColorAndOpacity(Picture ? FLinearColor::White : FLinearColor::Transparent);
	}
	const FVector2D WorldSize = Screen->GetScreenWorldSize();
	const float Aspect = WorldSize.Y > 1.0 ? FMath::Clamp(float(WorldSize.X / WorldSize.Y), 0.5f, 6.f) : 16.f / 9.f;
	PreviewSize->SetHeightOverride(FMath::Clamp(LeftColumnWidth / Aspect, 80.f, 300.f));
	// The seek bar follows playback unless it is being dragged; live streams have nothing to seek.
	const bool bSeekable = Item && Item->Duration > 0.f && !Item->bIsLive;
	SeekRow->SetVisibility(bSeekable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bSeekable && !bSeeking)
	{
		const double At = FMath::Clamp(Screen->GetPlaybackPosition(), 0.0, double(Item->Duration));
		SeekSlider->SetValue(float(At / Item->Duration));
		SeekPositionText->SetText(FormatTime(At));
		SeekDurationText->SetText(FormatTime(Item->Duration));
	}
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
	if (Player && !SubtitlesHandle.IsValid())
	{
		SubtitlesHandle = Player->OnSubtitlesChanged.AddUObject(this, &UTSScreenPanel::RebuildSubtitles);
	}
	RebuildSubtitles();
	const bool bBrowserMode = Screen->GetMode() == ETSScreenMode::Browser;
	const bool bWindowMode = Screen->GetMode() == ETSScreenMode::Window;
	const bool bRemoteMode = Screen->GetMode() == ETSScreenMode::RemoteDesktop;
	const bool bShared = bBrowserMode || bWindowMode || bRemoteMode;
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
			const UTSScreenPlayer* Me = Player;
			const ETSInputScope Granted = Me ? Me->GetLocalGrant() : ETSInputScope::None;
			Line = Granted != ETSInputScope::None ? FText::Format(LOCTEXT("ViewerGranted", "{0} lets you use it: open it in a window."), FText::FromString(Screen->GetControllerName()))
												  : LOCTEXT("ViewerPing", "Open it in a window and click to point at something for everyone.");
			if (!Stats.IsEmpty())
			{
				Line = FText::Format(LOCTEXT("ViewingStats", "{0}   Stream: {1}"), Line, Stats);
			}
		}
		SharingText->SetText(Line);
		Show(SharingText, !Line.IsEmpty());
		const bool bGranted = !bMine && Player && Player->GetLocalGrant() != ETSInputScope::None;
		TSVanillaUI::SetButtonText(OpenWindowButton, bMine || bGranted ? FText::Format(LOCTEXT("OpenWindowKey", "Use it in a window ({0})"), FText::FromString(FocusKey))
																	   : LOCTEXT("WatchWindow", "Watch in a window"));
		TSVanillaUI::SetButtonText(CurtainButton, Screen->IsCurtainOn() ? LOCTEXT("CurtainOff", "Show it again")
																		 : FText::Format(LOCTEXT("CurtainOnKey", "Blank for everyone ({0})"), FText::FromString(CurtainKey)));
		TSVanillaUI::SetButtonText(StopSharingButton, bWindowMode ? LOCTEXT("WindowStop", "Stop sharing")
														: bRemoteMode ? LOCTEXT("RemoteStop", "Disconnect")
																	  : LOCTEXT("BrowserStop", "Close the browser"));
	}
	Show(OpenWindowButton, bMine || (bShared && Player && Player->IsReceivingStream()));
	Show(ViewersBox, bMine);
	Show(ViewersHeader, bMine && Screen->GetViewerStates().Num() > 0);
	if (bMine)
	{
		RebuildViewers();
	}
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
		Show(TabButtons[int32(ETab::RemoteDesktop)], bWindowsAvailable);
	}
	if (bRemoteMode && bMine)
	{
		RemoteNote->SetText(FText::Format(LOCTEXT("RemoteMine",
			"Everyone near this screen sees this computer. Use it in its window ({0}): mouse and keyboard go to it over VNC. Connect to another "
			"address below to switch."), FText::FromString(FocusKey)));
	}
	else
	{
		RemoteNote->SetText(LOCTEXT("RemoteIdle",
			"Show a computer's desktop on this screen over VNC: a VM console (QEMU/KVM, Proxmox), TigerVNC, x11vnc, a Raspberry Pi... "
			"The address and password stay on your PC. Encrypted when the server offers VeNCrypt with a certificate (its certificate is "
			"remembered); plain VNC isn't encrypted: use that on your own network."));
	}
	const FText Security = Player ? Player->GetRemoteSecurity() : FText::GetEmpty();
	RemoteSecurityText->SetText(Security);
	Show(RemoteSecurityText, !Security.IsEmpty());
	Show(TrustCertificateButton, Player && Player->HasChangedCertificate());

	// Control log: for the sharer and the world host.
	const TArray<FString>& Log = Screen->GetControlLog();
	const bool bShowLog = bShared && (bMine || bHostTools) && Log.Num() > 0;
	Show(ControlLogHeader, bShowLog);
	Show(ControlLogText, bShowLog);
	if (bShowLog)
	{
		TArray<FString> Lines;
		for (int32 I = Log.Num() - 1; I >= 0 && Lines.Num() < 6; --I)
		{
			Lines.Add(Log[I]);
		}
		ControlLogText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
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
		BrowserNote->SetText(FText::Format(LOCTEXT("BrowserOtherNote", "{0} is using this screen's browser. You can open yours here instead if the world host allows it."),
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

void UTSScreenPanel::RebuildViewers()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	// Rebuilt only when a viewer, a grant or a grant's minutes change.
	FString Signature;
	for (const APlayerState* Viewer : Screen->GetViewerStates())
	{
		const FTSInputGrant* Grant = Screen->GetGrants().FindByPredicate([Viewer](const FTSInputGrant& Each) { return Each.Player == Viewer; });
		Signature += FString::Printf(TEXT("%s|%d|%d;"), Viewer ? *Viewer->GetPlayerName() : TEXT("?"), Grant ? int32(Grant->Scope) : 0,
			Grant ? FMath::CeilToInt((Grant->ExpiresAt - Screen->GetServerTime()) / 60.0) : 0);
	}
	if (Signature == ShownViewers)
	{
		return;
	}
	ShownViewers = Signature;
	ViewersBox->ClearChildren();
	const TArray<TObjectPtr<APlayerState>>& Viewers = Screen->GetViewerStates();
	for (int32 I = 0; I < Viewers.Num(); ++I)
	{
		const APlayerState* Viewer = Viewers[I];
		if (!Viewer)
		{
			continue;
		}
		const FTSInputGrant* Grant = Screen->GetGrants().FindByPredicate([Viewer](const FTSInputGrant& Each) { return Each.Player == Viewer; });
		FText Label = FText::Format(LOCTEXT("ViewerRow", "{0}: watching"), FText::FromString(Viewer->GetPlayerName()));
		if (Grant)
		{
			const FText Scope = Grant->Scope == ETSInputScope::Pointer ? LOCTEXT("GrantedPointer", "can use the mouse")
				: Grant->Scope == ETSInputScope::Typing				   ? LOCTEXT("GrantedTyping", "can use the mouse and type")
																	   : LOCTEXT("GrantedFull", "can use mouse and keyboard");
			Label = FText::Format(LOCTEXT("ViewerRowGranted", "{0}: {1} ({2} min left)"), FText::FromString(Viewer->GetPlayerName()), Scope,
				FText::AsNumber(FMath::Max(1, FMath::CeilToInt((Grant->ExpiresAt - Screen->GetServerTime()) / 60.0))));
		}
		ViewersBox->AddChildToVerticalBox(MakeText(Label, 13))->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));
		UWrapBox* Buttons = WidgetTree->ConstructWidget<UWrapBox>();
		Buttons->SetInnerSlotPadding(FVector2D(6.f, 4.f));
		Buttons->AddChildToWrapBox(MakeButton(LOCTEXT("GrantPointer", "Mouse"), ETSPanelAction::GrantPointer, I));
		Buttons->AddChildToWrapBox(MakeButton(LOCTEXT("GrantTyping", "Mouse and typing"), ETSPanelAction::GrantTyping, I));
		if (Screen->GetMode() == ETSScreenMode::Browser)
		{
			Buttons->AddChildToWrapBox(MakeButton(LOCTEXT("GrantFull", "Everything"), ETSPanelAction::GrantFull, I));
		}
		if (Grant)
		{
			Buttons->AddChildToWrapBox(MakeButton(LOCTEXT("GrantRevoke", "Revoke"), ETSPanelAction::GrantRevoke, I));
		}
		ViewersBox->AddChildToVerticalBox(Buttons);
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
		const FText Description = Info.Exe.IsEmpty() ? LOCTEXT("WindowRowShare", "Click to share") : FText::Format(LOCTEXT("WindowRowExe", "{0}   Click to share"),
			FText::FromString(Info.Exe));
		WindowListBox->AddChildToVerticalBox(MakeListButton(FText::FromString(Info.Title), Description, false, ETSPanelAction::WindowShare, I))->SetPadding(FMargin(0.f, 2.f));
	}
	RefreshSharing();
}

void UTSScreenPanel::AskConfirm(EConfirm Kind, int32 Index)
{
	const bool bWindow = Kind == EConfirm::ShareWindow;
	const FString& NoticeKey = bWindow							   ? UTSConfig::WindowDisclaimerKey
							 : Kind == EConfirm::ConnectRemote ? UTSConfig::RemoteDisclaimerKey
							 : Kind == EConfirm::Grant		   ? UTSConfig::GrantDisclaimerKey
															   : UTSConfig::StreamDisclaimerKey;
	// Control of a whole computer is confirmed every time; the other notices once.
	const ATSBuildableScreen* AskScreen = GetScreen();
	const bool bRemoteGrant = Kind == EConfirm::Grant && AskScreen && AskScreen->GetMode() == ETSScreenMode::RemoteDesktop;
	if (UTSConfig::GetBool(this, NoticeKey, false) && !bRemoteGrant)
	{
		RunConfirmed(Kind, Index);
		return;
	}
	PendingConfirm = Kind;
	PendingIndex = Index;
	const FText CurtainKey = FText::FromString(UTSConfig::GetString(this, UTSConfig::CurtainKeyKey, TEXT("F10")));
	if (Kind == EConfirm::Grant)
	{
		const ATSBuildableScreen* Screen = GetScreen();
		const APlayerState* Viewer = Screen && Screen->GetViewerStates().IsValidIndex(Index) ? Screen->GetViewerStates()[Index].Get() : nullptr;
		ConfirmTitle->SetText(bRemoteGrant ? LOCTEXT("ConfirmGrantRemoteTitle", "Give another player control of this computer?")
										   : LOCTEXT("ConfirmGrantTitle", "Give another player control?"));
		if (bRemoteGrant)
		{
			ConfirmText->SetText(FText::Format(LOCTEXT("ConfirmGrantRemoteText",
				"{0} will be able to click{1} on the remote computer, as you could: open programs, change settings, read files there. Only do "
				"this with people you trust.\n\nIt ends by itself after {2} minutes; Revoke or {3} ends it at once. Every grant is listed in "
				"the control log, and this question appears each time for remote desktops."),
				FText::FromString(Viewer ? Viewer->GetPlayerName() : FString(TEXT("They"))),
				PendingGrantScope == ETSInputScope::Pointer ? FText::GetEmpty() : LOCTEXT("ConfirmGrantRemoteTyping", " and type"),
				FText::AsNumber(FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::GrantMinutesKey, 15), 1, 120)), CurtainKey));
			Show(ConfirmLayer, true);
			return;
		}
		ConfirmText->SetText(FText::Format(LOCTEXT("ConfirmGrant",
			"{0} will be able to click{1} in your {2} on your PC. They can do anything you could do there, including in accounts you're signed in "
			"to. Only do this with people you trust.\n\nIt ends by itself after {3} minutes; Revoke or {4} ends it at once. This question appears "
			"only once."),
			FText::FromString(Viewer ? Viewer->GetPlayerName() : FString(TEXT("They"))),
			PendingGrantScope == ETSInputScope::Pointer ? FText::GetEmpty() : LOCTEXT("ConfirmGrantTyping", " and type"),
			Screen && Screen->GetMode() == ETSScreenMode::Window ? LOCTEXT("ConfirmGrantWindow", "window")
			: Screen && Screen->GetMode() == ETSScreenMode::RemoteDesktop ? LOCTEXT("ConfirmGrantRemote", "remote desktop")
																		 : LOCTEXT("ConfirmGrantBrowser", "browser"),
			FText::AsNumber(FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::GrantMinutesKey, 15), 1, 120)), CurtainKey));
	}
	else if (Kind == EConfirm::ConnectRemote)
	{
		ConfirmTitle->SetText(LOCTEXT("ConfirmRemoteTitle", "Show a whole computer to everyone?"));
		ConfirmText->SetText(FText::Format(LOCTEXT("ConfirmRemote",
			"You are sharing a whole computer: every player near this screen sees its desktop and hears its sound. Only you can control it.\n\n"
			"{0} blanks it for everyone at any time. This question appears only once."), CurtainKey));
	}
	else if (bWindow)
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
	case EConfirm::Grant:
		if (Screen->GetViewerStates().IsValidIndex(Index))
		{
			RCO->Server_SetInputGrant(Screen, Screen->GetViewerStates()[Index], PendingGrantScope, FMath::Clamp(UTSConfig::GetInt(this, UTSConfig::GrantMinutesKey, 15), 1, 120));
		}
		break;
	case EConfirm::ConnectRemote:
	{
		FTSRemoteConnection Connection;
		if (Index != INDEX_NONE && SavedRemotes.IsValidIndex(Index))
		{
			Connection = SavedRemotes[Index];
		}
		else if (!ReadRemoteFields(Connection))
		{
			SetNotice(LOCTEXT("RemoteNoAddress", "Type the VNC address first, e.g. 192.168.1.20:5901."), true);
			break;
		}
		ConnectRemote(Connection);
		break;
	}
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

bool UTSScreenPanel::ReadRemoteFields(FTSRemoteConnection& Out)
{
	if (!TSRemoteConnections::Parse(TSVanillaUI::GetInputText(RemoteAddressBox).ToString(), TSVanillaUI::GetInputText(RemotePasswordBox).ToString(), Out))
	{
		return false;
	}
	Out.Username = TSVanillaUI::GetInputText(RemoteUserBox).ToString().TrimStartAndEnd().Left(128);
	return true;
}

void UTSScreenPanel::ConnectRemote(const FTSRemoteConnection& Connection)
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	if (!Screen || !RCO)
	{
		return;
	}
	const FString Token = TSRemoteConnections::MakePending(Connection);
	UE_LOG(LogTelescreen, Log, TEXT("%s: connecting to VNC %s"), *Screen->GetName(), *Connection.GetAddress());
	RCO->Server_TakeControl(Screen, ETSScreenMode::RemoteDesktop, Token);
	// VNC's own password login only uses the first 8 characters (VeNCrypt's user login uses all of it).
	SetNotice(Connection.Password.Len() > 8 && Connection.Username.IsEmpty()
		? FText::Format(LOCTEXT("RemoteStartingLongPassword", "Connecting to {0}... Note: VNC passwords only count their first 8 characters."),
			FText::FromString(Connection.GetAddress()))
		: FText::Format(LOCTEXT("RemoteStarting", "Connecting to {0}..."), FText::FromString(Connection.GetAddress())));
}

void UTSScreenPanel::ShowSavedRemotes()
{
	RemoteListBox->ClearChildren();
	if (SavedRemotes.Num() == 0)
	{
		RemoteListBox->AddChildToVerticalBox(MakeText(LOCTEXT("RemoteNoneSaved", "None yet: type an address and press Save."), 13, Muted));
		return;
	}
	for (int32 I = 0; I < SavedRemotes.Num(); ++I)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		const FText Description = SavedRemotes[I].Password.IsEmpty() ? LOCTEXT("RemoteRowNoPassword", "Click to connect") : LOCTEXT("RemoteRowPassword", "Password saved   Click to connect");
		UHorizontalBoxSlot* ItemSlot = Row->AddChildToHorizontalBox(MakeListButton(FText::FromString(SavedRemotes[I].GetAddress()), Description, false,
			ETSPanelAction::RemoteSavedConnect, I));
		ItemSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ItemSlot->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(MakeButton(LOCTEXT("RemoteRowRemove", "Remove"), ETSPanelAction::RemoteSavedRemove, I))->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		RemoteListBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 2.f));
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
		const FText Title = FText::Format(LOCTEXT("QueueRow", "{0}. {1}"), FText::AsNumber(I + 1), FText::FromString(Item.GetDisplayTitle()));
		TArray<FString> Details;
		if (bCurrent)
		{
			Details.Add(LOCTEXT("QueueRowNow", "Playing").ToString());
		}
		if (Item.bIsLive)
		{
			Details.Add(LOCTEXT("QueueRowLive", "Live").ToString());
		}
		else if (Item.Duration > 0.f)
		{
			Details.Add(FormatTime(Item.Duration).ToString());
		}
		if (!Item.AddedBy.IsEmpty())
		{
			Details.Add(FText::Format(LOCTEXT("QueueRowBy", "added by {0}"), FText::FromString(Item.AddedBy)).ToString());
		}
		if (!bCurrent)
		{
			Details.Add(LOCTEXT("QueueRowPlay", "Click to play").ToString());
		}
		UHorizontalBoxSlot* ItemSlot = Row->AddChildToHorizontalBox(MakeListButton(Title, FText::FromString(FString::Join(Details, TEXT("   "))), bCurrent,
			ETSPanelAction::PlayIndex, I));
		ItemSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ItemSlot->SetVerticalAlignment(VAlign_Center);
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
		SetNotice(FText::Format(LOCTEXT("AddedFile", "Added {0}. Players who have it (same path, or same file name in their videos folder) play their own copy; others watch it streamed from a player who has it."),
			FText::FromString(Item.Title)));
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
		SelectTab(ETab(FMath::Clamp(Index, 0, int32(ETab::Screen))));
		return;
	case ETSPanelAction::ConfirmAccept:
	{
		const EConfirm Kind = PendingConfirm;
		const int32 ConfirmIndex = PendingIndex;
		PendingConfirm = EConfirm::None;
		Show(ConfirmLayer, false);
		UTSConfig::SetBool(this, Kind == EConfirm::ShareWindow	   ? UTSConfig::WindowDisclaimerKey
								 : Kind == EConfirm::ConnectRemote ? UTSConfig::RemoteDisclaimerKey
								 : Kind == EConfirm::Grant		   ? UTSConfig::GrantDisclaimerKey
																   : UTSConfig::StreamDisclaimerKey, true);
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
	case ETSPanelAction::SubtitleMode:
	{
		UTSScreenPlayer::SetSubtitleMode(this, UTSScreenPlayer::ESubtitleMode(FMath::Clamp(Index, 0, 3)));
		for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
		{
			if (UTSScreenPlayer* Each = It->GetPlayer())
			{
				Each->RefreshSubtitles();
			}
		}
		RebuildSubtitles();
		return;
	}
	case ETSPanelAction::RemoteTrustCertificate:
		if (UTSScreenPlayer* Player = Screen ? Screen->GetPlayer() : nullptr)
		{
			Player->TrustChangedCertificate();
			SetNotice(LOCTEXT("CertificateTrusted", "The new certificate is trusted for that address. Connect again."));
		}
		RefreshSharing();
		return;
	case ETSPanelAction::SubtitleTrack:
		if (UTSScreenPlayer* Player = Screen ? Screen->GetPlayer() : nullptr)
		{
			if (UTSScreenPlayer::GetSubtitleMode(this) == UTSScreenPlayer::ESubtitleMode::Off)
			{
				UTSScreenPlayer::SetSubtitleMode(this, UTSScreenPlayer::ESubtitleMode::Screen);
			}
			Player->SelectSubtitleTrack(Player->GetSelectedSubtitleTrack() == uint32(Index) ? 0 : uint32(Index));
			Player->RefreshSubtitles();
		}
		RebuildSubtitles();
		return;
	case ETSPanelAction::PickFile:
	{
		TWeakObjectPtr<UTSScreenPanel> WeakThis(this);
		TSLocalVideos::PickFile(TSLocalVideos::GetFolder(this), [WeakThis](const FString& Path)
		{
			if (UTSScreenPanel* Panel = WeakThis.Get(); Panel && !Path.IsEmpty())
			{
				TSVanillaUI::SetInputText(Panel->LinkBox, FText::FromString(Path));
				Panel->SetNotice(LOCTEXT("PickedFile", "Press Play now or Add to queue."));
			}
		});
		return;
	}
	case ETSPanelAction::ChangeVideosFolder:
	{
		TWeakObjectPtr<UTSScreenPanel> WeakThis(this);
		TSLocalVideos::PickFolder(TSLocalVideos::GetFolder(this), [WeakThis](const FString& Path)
		{
			if (UTSScreenPanel* Panel = WeakThis.Get(); Panel && !Path.IsEmpty())
			{
				TSLocalVideos::SetFolder(Panel, Path);
				Panel->RefreshVideosFolder();
				Panel->SetNotice(LOCTEXT("FolderChanged", "Videos folder changed. Files are matched by name in it and its subfolders."));
				if (ATSBuildableScreen* Screen = Panel->GetScreen(); Screen && Screen->GetPlayer())
				{
					Screen->GetPlayer()->RefreshMedia();
				}
			}
		});
		return;
	}
	case ETSPanelAction::OpenVideosFolder:
		if (TSLocalVideos::GetFolder(this).IsEmpty())
		{
			SetNotice(LOCTEXT("NoFolderToOpen", "Choose a videos folder first (Change folder)."), true);
		}
		else
		{
			TSLocalVideos::OpenFolder(this);
		}
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
	case ETSPanelAction::RemoteConnect: AskConfirm(EConfirm::ConnectRemote, INDEX_NONE); break;
	case ETSPanelAction::GrantPointer:
	case ETSPanelAction::GrantTyping:
	case ETSPanelAction::GrantFull:
		PendingGrantScope = Action == ETSPanelAction::GrantPointer ? ETSInputScope::Pointer : Action == ETSPanelAction::GrantTyping ? ETSInputScope::Typing : ETSInputScope::Full;
		AskConfirm(EConfirm::Grant, Index);
		break;
	case ETSPanelAction::GrantRevoke:
		if (Screen->GetViewerStates().IsValidIndex(Index))
		{
			RCO->Server_SetInputGrant(Screen, Screen->GetViewerStates()[Index], ETSInputScope::None, 0);
		}
		break;
	case ETSPanelAction::RemoteSavedConnect: AskConfirm(EConfirm::ConnectRemote, Index); break;
	case ETSPanelAction::RemoteSave:
	{
		FTSRemoteConnection Connection;
		if (!ReadRemoteFields(Connection))
		{
			SetNotice(LOCTEXT("RemoteNoAddressSave", "Type the VNC address first."), true);
			break;
		}
		SavedRemotes.RemoveAll([&Connection](const FTSRemoteConnection& Each) { return Each.Host == Connection.Host && Each.Port == Connection.Port; });
		SavedRemotes.Add(Connection);
		TSRemoteConnections::Save(SavedRemotes);
		ShowSavedRemotes();
		SetNotice(FText::Format(LOCTEXT("RemoteSaved", "Saved {0}."), FText::FromString(Connection.GetAddress())));
		break;
	}
	case ETSPanelAction::RemoteSavedRemove:
		if (SavedRemotes.IsValidIndex(Index))
		{
			SavedRemotes.RemoveAt(Index);
			TSRemoteConnections::Save(SavedRemotes);
			ShowSavedRemotes();
		}
		break;
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
