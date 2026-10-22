#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "TSContent.generated.h"

namespace TSContent
{
	// Loads the vanilla assets (meshes, hologram, icons, items, categories) into Telescreen's class defaults.
	void ResolveVanillaAssets();
}

// Shared descriptor setup for both screen sizes.
UCLASS(Abstract)
class TELESCREEN_API UTSDesc_Screen : public UFGBuildingDescriptor
{
	GENERATED_BODY()

public:
	UTSDesc_Screen();

protected:
	friend void TSContent::ResolveVanillaAssets();

	// Points this descriptor's icons and build menu category at the vanilla billboard ones.
	void ApplyVanillaAssets(const TCHAR* IconPath);
};

UCLASS()
class TELESCREEN_API UTSDesc_ScreenSmall : public UTSDesc_Screen
{
	GENERATED_BODY()

public:
	UTSDesc_ScreenSmall();
};

UCLASS()
class TELESCREEN_API UTSDesc_ScreenLarge : public UTSDesc_Screen
{
	GENERATED_BODY()

public:
	UTSDesc_ScreenLarge();
};

// Shared build-gun recipe setup for both screen sizes.
UCLASS(Abstract)
class TELESCREEN_API UTSRecipe_Screen : public UFGRecipe
{
	GENERATED_BODY()

public:
	UTSRecipe_Screen();

protected:
	friend void TSContent::ResolveVanillaAssets();

	// Sets the ingredients, matching the vanilla billboard of the same size.
	void ApplyIngredients(int32 ReinforcedPlates, int32 CopperSheets, int32 CrystalOscillators);
};

UCLASS()
class TELESCREEN_API UTSRecipe_ScreenSmall : public UTSRecipe_Screen
{
	GENERATED_BODY()

public:
	UTSRecipe_ScreenSmall();
};

UCLASS()
class TELESCREEN_API UTSRecipe_ScreenLarge : public UTSRecipe_Screen
{
	GENERATED_BODY()

public:
	UTSRecipe_ScreenLarge();
};

// Unlocks both screen recipes.
UCLASS()
class TELESCREEN_API UTSUnlockRecipe_Screens : public UFGUnlockRecipe
{
	GENERATED_BODY()

public:
	UTSUnlockRecipe_Screens();
};

// AWESOME Shop entry that unlocks both Telescreen sizes.
UCLASS()
class TELESCREEN_API UTSSchematic_Screens : public UFGSchematic
{
	GENERATED_BODY()

public:
	UTSSchematic_Screens();

protected:
	friend void TSContent::ResolveVanillaAssets();

	// Sets the coupon cost, shop category and icon from vanilla assets.
	void ApplyVanillaAssets();
};
