#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableSignBase.h"
#include "TSBuildableScreen.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture;

// A Telescreen: a billboard-shaped sign whose face shows a screen texture.
UCLASS(Abstract)
class TELESCREEN_API ATSBuildableScreen : public AFGBuildableSignBase
{
	GENERATED_BODY()

public:
	ATSBuildableScreen();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual bool IsUseable_Implementation() const override;
	virtual void OnUse_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) override;
	virtual FText GetLookAtDecription_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) const override;

	// Shows Texture on the screen face; nullptr shows the test pattern.
	void SetScreenTexture(UTexture* Texture);

	// Size of the screen face in world units, including the actor's scale.
	FVector2D GetScreenWorldSize() const;

	// Loads the vanilla billboard assets into the class defaults of both screen sizes.
	static void ResolveVanillaAssets();

protected:
	// Applies the billboard mesh, screen plane, screen material and hologram to this class default object.
	void ApplyVanillaAssets(const TCHAR* HologramPath);

	// Sets the transforms, dimensions and clearance copied from the vanilla billboard of this size.
	void ConfigureBillboardLayout(const FVector& MeshLocation, float MeshScale, const FVector& PlaneLocation, const FVector2D& PlaneSize,
		const FVector2D& WorldDimensions, const FVector& PoleOffset, const FVector2D& PoleScale, float SignToSignOffset, const FBox& ClearanceBox, const FVector& ClearanceOffset);

	UPROPERTY(VisibleAnywhere, Category = "Telescreen")
	TObjectPtr<UStaticMeshComponent> mScreenPlane;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> mScreenMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> mScreenMID;

	// Size of the unscaled screen plane in world units.
	FVector2D mScreenPlaneSize;

	int32 mTestPatternVariant = 0;
};

// Small Telescreen, built from the Small Billboard (8 m x 4 m).
UCLASS()
class TELESCREEN_API ATSBuildableScreenSmall : public ATSBuildableScreen
{
	GENERATED_BODY()

public:
	ATSBuildableScreenSmall();
};

// Large Telescreen, built from the Large Billboard (16 m x 8 m).
UCLASS()
class TELESCREEN_API ATSBuildableScreenLarge : public ATSBuildableScreen
{
	GENERATED_BODY()

public:
	ATSBuildableScreenLarge();
};
