#pragma once

#include "CoreMinimal.h"
#include "Buildables/TSBuildableScreen.h"
#include "FGRemoteCallObject.h"
#include "TSTypes.h"
#include "TSRemoteCallObject.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FTSOnNotice, const FText&);

// Carries a player's screen edits to the server and the server's refusals back.
UCLASS(NotBlueprintable)
class TELESCREEN_API UTSRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Enqueue(ATSBuildableScreen* Screen, const FTSMediaItem& Item, bool bPlayNow);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_MediaCommand(ATSBuildableScreen* Screen, ETSMediaCommand Command, int32 Index, double Seconds);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetScreenSettings(ATSBuildableScreen* Screen, const FString& Name, ETSAspectMode AspectMode);

	// Whether this player can play the current synced item Url (see ATSBuildableScreen::Server_ReportMedia).
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportMedia(ATSBuildableScreen* Screen, const FString& Url, bool bHave, bool bCanStream);

	// The screen's volume for everyone (sent when the slider is let go).
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetScreenVolume(ATSBuildableScreen* Screen, float Volume);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_TakeControl(ATSBuildableScreen* Screen, ETSScreenMode Mode, const FString& Url);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReleaseControl(ATSBuildableScreen* Screen);

	// Live stream: the controller sends chunks up (unreliable); the server relays them to the watchers. Losses are
	// repaired with keyframes, so nothing here is reliable.
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_StreamChunk(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk);

	UFUNCTION(Client, Unreliable)
	void Client_StreamChunk(ATSBuildableScreen* Screen, const FTSStreamChunk& Chunk);

	// Direct links (see Docs/Architecture.md, Direct connections): a viewer offers its addresses; the server makes a
	// link id and key and asks the stream sender, whose answer goes back to the viewer; the viewer reports when the
	// link is up (the server then stops relaying to it) or down.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_DirectRequest(ATSBuildableScreen* Screen, const TArray<FString>& Candidates);

	UFUNCTION(Client, Reliable)
	void Client_DirectRequest(ATSBuildableScreen* Screen, uint32 Link, const TArray<uint8>& Key, const TArray<FString>& Candidates);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_DirectAnswer(ATSBuildableScreen* Screen, uint32 Link, const TArray<FString>& Candidates);

	UFUNCTION(Client, Reliable)
	void Client_DirectAnswer(ATSBuildableScreen* Screen, uint32 Link, const TArray<uint8>& Key, const TArray<FString>& Candidates);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetDirect(ATSBuildableScreen* Screen, bool bDirect);

	// A viewer starts or stops watching a screen's stream.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetWatching(ATSBuildableScreen* Screen, bool bWatching);

	// The controller turns the privacy curtain on or off.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetCurtain(ATSBuildableScreen* Screen, bool bOn);

	// A viewer lost part of the stream and needs a keyframe; the server forwards it to the controller.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestKeyframe(ATSBuildableScreen* Screen);

	UFUNCTION(Client, Reliable)
	void Client_RequestKeyframe(ATSBuildableScreen* Screen);

	// The controller lets Viewer use the source (Scope None revokes) for Minutes.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetInputGrant(ATSBuildableScreen* Screen, APlayerState* Viewer, ETSInputScope Scope, int32 Minutes);

	// A granted viewer's input: pointer moves unreliably, everything else reliably.
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_ViewerPointer(ATSBuildableScreen* Screen, const FTSViewerInput& Input);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ViewerInput(ATSBuildableScreen* Screen, const FTSViewerInput& Input);

	UFUNCTION(Client, Reliable)
	void Client_ViewerInput(ATSBuildableScreen* Screen, const FTSViewerInput& Input, const FString& ViewerName, ETSInputScope Scope);

	UFUNCTION(Client, Unreliable)
	void Client_ViewerPointer(ATSBuildableScreen* Screen, const FTSViewerInput& Input, const FString& ViewerName, ETSInputScope Scope);

	// A viewer points at a spot of the picture (no control needed); everyone watching sees a named ping.
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Ping(ATSBuildableScreen* Screen, uint16 X, uint16 Y);

	// A named cursor or ping on Screen (positions 0-65535 across the picture).
	UFUNCTION(Client, Unreliable)
	void Client_ScreenMarker(ATSBuildableScreen* Screen, const FString& Name, uint16 X, uint16 Y, bool bPing);

	// Server: asks Screen's controller for a keyframe.
	static void SendKeyframeRequest(ATSBuildableScreen* Screen);

	// Tells the player why the server refused an edit.
	UFUNCTION(Client, Reliable)
	void Client_Notice(const FString& Message);

	// True if Controller is the host (listen server's own player) or a dedicated server admin.
	static bool IsHostOrAdmin(const APlayerController* Controller);

	// True if the local player is the host or an admin (for showing host tools).
	static bool IsLocalHostOrAdmin(const UObject* WorldContext);

	// Returns the local player's instance, or nullptr (logged) if the server doesn't run the mod.
	static UTSRemoteCallObject* GetLocal(const UObject* WorldContext);

	// Broadcast on the client when the server sends a notice.
	static FTSOnNotice OnNotice;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	bool MayEdit(const ATSBuildableScreen* Screen);
	bool MayUseMode(const ATSBuildableScreen* Screen, ETSScreenMode Mode);
	// Playing a video on a screen that shows a shared source switches it back to videos: always for its sharer and the
	// world host, for others only if taking over is allowed. Returns false (with a notice) if not allowed.
	bool ReturnToVideos(ATSBuildableScreen* Screen);
	FString GetRequesterName() const;

	UPROPERTY(Replicated, Meta = (NoAutoJson))
	bool mForceNetField_UTSRemoteCallObject = false;

	TArray<double> RecentAdds;
	TArray<double> RecentPings;
	double LastCursorBroadcast = 0.0;
	void RelayViewerInput(ATSBuildableScreen* Screen, const FTSViewerInput& Input, bool bReliable);
	// Server: sends a cursor or ping marker to the controller and every watcher of Screen.
	static void BroadcastMarker(ATSBuildableScreen* Screen, const FString& Name, uint16 X, uint16 Y, bool bPing);
};
