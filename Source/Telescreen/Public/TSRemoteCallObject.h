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

	// Tells the player why the server refused an edit.
	UFUNCTION(Client, Reliable)
	void Client_Notice(const FString& Message);

	// Returns the local player's instance, or nullptr (logged) if the server doesn't run the mod.
	static UTSRemoteCallObject* GetLocal(const UObject* WorldContext);

	// Broadcast on the client when the server sends a notice.
	static FTSOnNotice OnNotice;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	bool MayEdit(const ATSBuildableScreen* Screen);
	FString GetRequesterName() const;

	UPROPERTY(Replicated, Meta = (NoAutoJson))
	bool mForceNetField_UTSRemoteCallObject = false;

	TArray<double> RecentAdds;
};
