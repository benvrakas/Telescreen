#include "UI/TSScreenPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/TSBuildableScreen.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FGHUD.h"
#include "GameFramework/PlayerController.h"
#include "Host/TSHostSubsystem.h"
#include "Screen/TSScreenPlayer.h"
#include "Telescreen.h"
#include "TSRemoteCallObject.h"
#include "UI/FGGameUI.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	const FLinearColor Accent(0.95f, 0.55f, 0.1f);
	const FLinearColor Muted(0.65f, 0.7f, 0.76f);
	const FLinearColor ErrorColor(1.f, 0.45f, 0.4f);

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
}

void UTSPanelAction::HandleClicked()
{
	if (Panel)
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

UTextBlock* UTSScreenPanel::MakeText(const FText& Text, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
	Block->SetText(Text);
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = Size;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	return Block;
}

UWidget* UTSScreenPanel::MakeButton(const FText& Label, ETSPanelAction Action, int32 Index, const FLinearColor& Color)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(Color);
	UTextBlock* Text = MakeText(Label, 13);
	Text->SetAutoWrapText(false);
	Button->AddChild(Text);
	UTSPanelAction* Handler = NewObject<UTSPanelAction>(this);
	Handler->Panel = this;
	Handler->Action = Action;
	Handler->Index = Index;
	Button->OnClicked.AddDynamic(Handler, &UTSPanelAction::HandleClicked);
	Actions.Add(Handler);
	return Button;
}

void UTSScreenPanel::BuildTree()
{
	auto AddRow = [this](UVerticalBox* Box, UWidget* Child, float Top = 6.f)
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
		Slot->SetPadding(FMargin(0.f, Top, 0.f, 0.f));
		return Slot;
	};
	auto MakeHBox = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(); };
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

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(860.f);
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.045f, 0.97f));
	Background->SetPadding(FMargin(20.f));
	Size->AddChild(Background);
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	Background->AddChild(Root);
	WidgetTree->RootWidget = Size;

	UHorizontalBox* Header = MakeHBox();
	TitleText = MakeText(FText::GetEmpty(), 24);
	AddToHBox(Header, TitleText, true);
	AddToHBox(Header, MakeButton(LOCTEXT("Close", "Close"), ETSPanelAction::Close));
	AddRow(Root, Header, 0.f);

	NowPlayingText = MakeText(FText::GetEmpty(), 17);
	AddRow(Root, NowPlayingText, 10.f);
	TimeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Root, TimeText, 2.f);

	UHorizontalBox* Transport = MakeHBox();
	AddToHBox(Transport, MakeButton(LOCTEXT("PlayPause", "Play / Pause"), ETSPanelAction::PlayPause, INDEX_NONE, FLinearColor(0.25f, 0.15f, 0.05f)));
	AddToHBox(Transport, MakeButton(LOCTEXT("Back10", "-10 s"), ETSPanelAction::Back10));
	AddToHBox(Transport, MakeButton(LOCTEXT("Forward10", "+10 s"), ETSPanelAction::Forward10));
	AddToHBox(Transport, MakeButton(LOCTEXT("Next", "Next"), ETSPanelAction::Next));
	AddToHBox(Transport, MakeButton(LOCTEXT("Stop", "Stop"), ETSPanelAction::Stop));
	AddRow(Root, Transport, 8.f);

	AddRow(Root, MakeText(LOCTEXT("AddHeader", "Add a video"), 14, Accent), 16.f);
	UHorizontalBox* AddBox = MakeHBox();
	LinkBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	LinkBox->SetHintText(LOCTEXT("LinkHint", "Paste a YouTube, Twitch or Vimeo link, or a video file path"));
	AddToHBox(AddBox, LinkBox, true);
	AddToHBox(AddBox, MakeButton(LOCTEXT("PlayNow", "Play now"), ETSPanelAction::PlayNow, INDEX_NONE, FLinearColor(0.25f, 0.15f, 0.05f)));
	AddToHBox(AddBox, MakeButton(LOCTEXT("AddToQueue", "Add to queue"), ETSPanelAction::AddToQueue));
	AddRow(Root, AddBox);
	NoticeText = MakeText(FText::GetEmpty(), 13, Muted);
	AddRow(Root, NoticeText, 4.f);
	LoadAnywayButton = MakeButton(LOCTEXT("LoadAnyway", "Load it anyway on this PC"), ETSPanelAction::LoadAnyway);
	LoadAnywayButton->SetVisibility(ESlateVisibility::Collapsed);
	AddRow(Root, LoadAnywayButton, 4.f);

	UHorizontalBox* QueueHeader = MakeHBox();
	AddToHBox(QueueHeader, MakeText(LOCTEXT("QueueHeader", "Queue"), 14, Accent), true);
	AddToHBox(QueueHeader, MakeButton(LOCTEXT("ClearQueue", "Clear"), ETSPanelAction::ClearQueue));
	AddRow(Root, QueueHeader, 16.f);
	USizeBox* QueueSize = WidgetTree->ConstructWidget<USizeBox>();
	QueueSize->SetMaxDesiredHeight(260.f);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	QueueBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(QueueBox);
	QueueSize->AddChild(Scroll);
	AddRow(Root, QueueSize, 4.f);

	AddRow(Root, MakeText(LOCTEXT("SettingsHeader", "Screen settings"), 14, Accent), 16.f);
	UHorizontalBox* NameRow = MakeHBox();
	NameBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	NameBox->SetHintText(LOCTEXT("NameHint", "Screen name, e.g. Main Hall"));
	AddToHBox(NameRow, NameBox, true);
	AddToHBox(NameRow, MakeButton(LOCTEXT("SaveName", "Save name"), ETSPanelAction::SaveName));
	AddRow(Root, NameRow);
	UHorizontalBox* AspectRow = MakeHBox();
	AspectText = MakeText(FText::GetEmpty(), 13, Muted);
	AddToHBox(AspectRow, AspectText, true);
	AddToHBox(AspectRow, MakeButton(LOCTEXT("Letterbox", "Letterbox"), ETSPanelAction::AspectLetterbox));
	AddToHBox(AspectRow, MakeButton(LOCTEXT("Fill", "Fill"), ETSPanelAction::AspectFill));
	AddToHBox(AspectRow, MakeButton(LOCTEXT("Stretch", "Stretch"), ETSPanelAction::AspectStretch));
	AddRow(Root, AspectRow);
}

ATSBuildableScreen* UTSScreenPanel::GetScreen() const
{
	return Cast<ATSBuildableScreen>(mInteractObject);
}

void UTSScreenPanel::NativeConstruct()
{
	Super::NativeConstruct();
	if (ATSBuildableScreen* Screen = GetScreen())
	{
		ScreenChangedHandle = Screen->OnScreenChanged.AddUObject(this, &UTSScreenPanel::Refresh);
		NameBox->SetText(FText::FromString(Screen->GetScreenName()));
	}
	NoticeHandle = UTSRemoteCallObject::OnNotice.AddWeakLambda(this, [this](const FText& Text) { SetNotice(Text, true); });
	Refresh();
	UE_LOG(LogTelescreen, Log, TEXT("Screen panel opened for %s"), *GetNameSafe(GetScreen()));
}

void UTSScreenPanel::NativeDestruct()
{
	if (ATSBuildableScreen* Screen = GetScreen())
	{
		Screen->OnScreenChanged.Remove(ScreenChangedHandle);
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

void UTSScreenPanel::Refresh()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	TitleText->SetText(Screen->GetDisplayName());
	const FTSMediaState& State = Screen->GetMediaState();
	const FTSMediaItem* Item = State.GetCurrent();
	NowPlayingText->SetText(Item ? FText::Format(LOCTEXT("NowPlaying", "Now playing: {0}"), FText::FromString(Item->GetDisplayTitle()))
								 : LOCTEXT("NothingPlaying", "Nothing playing"));
	AspectText->SetText(FText::Format(LOCTEXT("AspectNow", "Picture fit: {0}"), AspectName(Screen->GetAspectMode())));
	if (State.Revision != ShownRevision)
	{
		ShownRevision = State.Revision;
		RebuildQueue();
	}
	RefreshTimes();
}

void UTSScreenPanel::RefreshTimes()
{
	ATSBuildableScreen* Screen = GetScreen();
	if (!Screen)
	{
		return;
	}
	const FTSMediaState& State = Screen->GetMediaState();
	const FTSMediaItem* Item = State.GetCurrent();
	FText Times = FText::GetEmpty();
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
	const UTSScreenPlayer* Player = Screen->GetPlayer();
	const FText Local = Player ? Player->GetLocalStatus() : FText::GetEmpty();
	if (!Local.IsEmpty())
	{
		Times = FText::Format(LOCTEXT("TimesWithStatus", "{0}   {1}"), Times, Local);
	}
	TimeText->SetText(Times);
	const bool bBlocked = Player && !Player->GetBlockedReason().IsEmpty();
	LoadAnywayButton->SetVisibility(bBlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	const FString Url = LinkBox->GetText().ToString().TrimStartAndEnd().TrimQuotes();
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
		LinkBox->SetText(FText::GetEmpty());
		SetNotice(FText::Format(LOCTEXT("AddedFile", "Added {0}. Other players need the same file at the same path."), FText::FromString(Item.Title)));
		return;
	}
	UTSHostSubsystem* Host = UTSHostSubsystem::Get(this);
	if (!Host || !Host->EnsureConnected())
	{
		RCO->Server_Enqueue(Screen, Item, bPlayNow);
		LinkBox->SetText(FText::GetEmpty());
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
		Panel->LinkBox->SetText(FText::GetEmpty());
		Panel->SetNotice(FText::Format(LOCTEXT("Added", "Added \"{0}\"."), FText::FromString(Result.Title)));
	});
}

void UTSScreenPanel::HandleAction(ETSPanelAction Action, int32 Index)
{
	ATSBuildableScreen* Screen = GetScreen();
	UTSRemoteCallObject* RCO = UTSRemoteCallObject::GetLocal(this);
	if (Action == ETSPanelAction::Close)
	{
		Close();
		return;
	}
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
		RCO->Server_SetScreenSettings(Screen, NameBox->GetText().ToString().Left(64), Screen->GetAspectMode());
		break;
	case ETSPanelAction::AspectLetterbox: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Letterbox); break;
	case ETSPanelAction::AspectFill: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Fill); break;
	case ETSPanelAction::AspectStretch: RCO->Server_SetScreenSettings(Screen, Screen->GetScreenName(), ETSAspectMode::Stretch); break;
	case ETSPanelAction::LoadAnyway:
		if (UTSScreenPlayer* Player = Screen->GetPlayer())
		{
			Player->LoadAnyway();
		}
		break;
	default: break;
	}
}

#undef LOCTEXT_NAMESPACE
