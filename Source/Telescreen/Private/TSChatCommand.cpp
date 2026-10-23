#include "TSChatCommand.h"
#include "Buildables/TSBuildableScreen.h"
#include "Command/CommandSender.h"
#include "EngineUtils.h"
#include "FGPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Telescreen.h"
#include "TSRemoteCallObject.h"

namespace
{
	FString DescribeMode(const ATSBuildableScreen* Screen)
	{
		const FString Who = Screen->GetControllerName();
		switch (Screen->GetMode())
		{
		case ETSScreenMode::Browser: return FString::Printf(TEXT("%s's browser"), *Who);
		case ETSScreenMode::Window: return FString::Printf(TEXT("a window from %s"), *Who);
		case ETSScreenMode::RemoteDesktop: return FString::Printf(TEXT("a remote desktop from %s"), *Who);
		default: break;
		}
		const FTSMediaState& State = Screen->GetMediaState();
		const FTSMediaItem* Item = State.GetCurrent();
		if (!Item)
		{
			return State.Queue.Num() > 0 ? FString::Printf(TEXT("stopped, %d queued"), State.Queue.Num()) : FString(TEXT("nothing"));
		}
		const int32 At = FMath::Max(0, FMath::FloorToInt(Screen->GetPlaybackPosition()));
		FString Text = FString::Printf(TEXT("\"%s\" %s at %d:%02d"), *Item->GetDisplayTitle().Left(60), State.bPaused ? TEXT("paused") : TEXT("playing"), At / 60,
			At % 60);
		if (!Screen->GetMediaStreamerName().IsEmpty())
		{
			Text += FString::Printf(TEXT(", streamed by %s"), *Screen->GetMediaStreamerName());
		}
		return Text;
	}
}

ATSChatCommand::ATSChatCommand()
{
	CommandName = TEXT("ts");
	Aliases.Add(TEXT("telescreen"));
	MinNumberOfArguments = 1;
	bOnlyUsableByPlayer = false;
	Usage = NSLOCTEXT("Telescreen", "TsCommandUsage", "/ts list - every screen and what it shows | /ts stop <number|all> - end its sharing and stop its video");
}

TArray<ATSBuildableScreen*> ATSChatCommand::GetScreens() const
{
	TArray<ATSBuildableScreen*> Screens;
	for (TActorIterator<ATSBuildableScreen> It(GetWorld()); It; ++It)
	{
		Screens.Add(*It);
	}
	Screens.Sort([](const ATSBuildableScreen& A, const ATSBuildableScreen& B)
	{
		const FString NameA = A.GetDisplayName().ToString(), NameB = B.GetDisplayName().ToString();
		if (NameA != NameB)
		{
			return NameA < NameB;
		}
		return A.GetActorLocation().X != B.GetActorLocation().X ? A.GetActorLocation().X < B.GetActorLocation().X : A.GetActorLocation().Y < B.GetActorLocation().Y;
	});
	return Screens;
}

EExecutionStatus ATSChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	if (Arguments[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		return List(Sender);
	}
	if (Arguments[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
	{
		if (Arguments.Num() < 2)
		{
			Sender->SendChatMessage(TEXT("Which screen? /ts stop <number from /ts list> or /ts stop all"), FLinearColor::Red);
			return EExecutionStatus::BAD_ARGUMENTS;
		}
		return Stop(Sender, Arguments[1]);
	}
	if (Arguments[0].Equals(TEXT("stopall"), ESearchCase::IgnoreCase))
	{
		return Stop(Sender, TEXT("all"));
	}
	Sender->SendChatMessage(TEXT("Unknown option. Use /ts list or /ts stop <number|all>."), FLinearColor::Red);
	return EExecutionStatus::BAD_ARGUMENTS;
}

EExecutionStatus ATSChatCommand::List(UCommandSender* Sender)
{
	const TArray<ATSBuildableScreen*> Screens = GetScreens();
	if (Screens.Num() == 0)
	{
		Sender->SendChatMessage(TEXT("There are no Telescreens in this world."));
		return EExecutionStatus::COMPLETED;
	}
	const AFGPlayerController* Player = Sender->IsPlayerSender() ? Sender->GetPlayer() : nullptr;
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	Sender->SendChatMessage(FString::Printf(TEXT("%d Telescreen%s:"), Screens.Num(), Screens.Num() == 1 ? TEXT("") : TEXT("s")));
	for (int32 I = 0; I < Screens.Num(); ++I)
	{
		const ATSBuildableScreen* Screen = Screens[I];
		FString Line = FString::Printf(TEXT("%d. %s: %s"), I + 1, *Screen->GetDisplayName().ToString(), *DescribeMode(Screen));
		if (Screen->GetViewerCount() > 0)
		{
			Line += FString::Printf(TEXT(", %d watching"), Screen->GetViewerCount());
		}
		if (Screen->IsCurtainOn())
		{
			Line += TEXT(", blanked");
		}
		if (Pawn)
		{
			Line += FString::Printf(TEXT(", %d m away"), FMath::RoundToInt(FVector::Dist(Pawn->GetActorLocation(), Screen->GetActorLocation()) / 100.0));
		}
		Sender->SendChatMessage(Line);
	}
	return EExecutionStatus::COMPLETED;
}

EExecutionStatus ATSChatCommand::Stop(UCommandSender* Sender, const FString& Which)
{
	const TArray<ATSBuildableScreen*> Screens = GetScreens();
	// The server console, the world host and admins stop any screen; other players only the one they share.
	const AFGPlayerController* Player = Sender->IsPlayerSender() ? Sender->GetPlayer() : nullptr;
	const bool bHost = !Sender->IsPlayerSender() || UTSRemoteCallObject::IsHostOrAdmin(Player);
	const FString Name = Player && Player->PlayerState ? Player->PlayerState->GetPlayerName() : FString(TEXT("the server"));
	TArray<ATSBuildableScreen*> Targets;
	if (Which.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		Targets = Screens;
	}
	else
	{
		const int32 Number = FCString::Atoi(*Which);
		if (Number < 1 || Number > Screens.Num())
		{
			Sender->SendChatMessage(FString::Printf(TEXT("No screen %s; /ts list numbers them 1 to %d."), *Which, Screens.Num()), FLinearColor::Red);
			return EExecutionStatus::BAD_ARGUMENTS;
		}
		Targets.Add(Screens[Number - 1]);
	}
	int32 Stopped = 0, Refused = 0;
	for (ATSBuildableScreen* Screen : Targets)
	{
		const bool bShared = Screen->GetMode() != ETSScreenMode::SyncedMedia;
		const bool bPlaying = !bShared && Screen->GetMediaState().GetCurrent() != nullptr;
		if (!bShared && !bPlaying)
		{
			continue;
		}
		const bool bOwn = Player && bShared && Screen->GetController() == Player->PlayerState;
		if (!bHost && !bOwn)
		{
			++Refused;
			continue;
		}
		if (bShared)
		{
			Screen->Server_ReleaseControl(*FString::Printf(TEXT("/ts stop by %s"), *Name));
		}
		if (Screen->GetMediaState().GetCurrent())
		{
			Screen->ApplyMediaCommand(ETSMediaCommand::Stop, 0, 0.0);
		}
		++Stopped;
	}
	UE_LOG(LogTelescreen, Log, TEXT("Command: /ts stop %s by %s: %d stopped, %d refused"), *Which, *Name, Stopped, Refused);
	if (Refused > 0 && Stopped == 0)
	{
		Sender->SendChatMessage(TEXT("Only the world host or an admin can stop screens other players use."), FLinearColor::Red);
		return EExecutionStatus::INSUFFICIENT_PERMISSIONS;
	}
	FString Message = Stopped == 0 ? FString(TEXT("Nothing was playing or shared there."))
								   : FString::Printf(TEXT("Stopped %d screen%s (queues kept: press Play to start again)."), Stopped, Stopped == 1 ? TEXT("") : TEXT("s"));
	if (Refused > 0)
	{
		Message += FString::Printf(TEXT(" %d used by others need the world host."), Refused);
	}
	Sender->SendChatMessage(Message);
	return EExecutionStatus::COMPLETED;
}
