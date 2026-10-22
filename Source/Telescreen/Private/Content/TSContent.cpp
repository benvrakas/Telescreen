#include "Content/TSContent.h"
#include "Buildables/TSBuildableScreen.h"
#include "Content/TSAssetPaths.h"
#include "Engine/Texture2D.h"
#include "FGCategory.h"
#include "FGSchematicCategory.h"
#include "Telescreen.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	template <typename T>
	TSubclassOf<T> LoadVanillaClass(const TCHAR* Path)
	{
		UClass* Class = LoadClass<T>(nullptr, Path);
		if (!Class)
		{
			UE_LOG(LogTelescreen, Error, TEXT("Could not load vanilla class %s"), Path);
		}
		return Class;
	}

	UTexture2D* LoadVanillaTexture(const TCHAR* Path)
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Path);
		if (!Texture)
		{
			UE_LOG(LogTelescreen, Error, TEXT("Could not load vanilla texture %s"), Path);
		}
		return Texture;
	}
}

void TSContent::ResolveVanillaAssets()
{
	UE_LOG(LogTelescreen, Log, TEXT("Resolving vanilla assets for Telescreen content"));

	ATSBuildableScreen::ResolveVanillaAssets();

	GetMutableDefault<UTSDesc_ScreenSmall>()->ApplyVanillaAssets(TSAssetPaths::IconSmallBillboard);
	GetMutableDefault<UTSDesc_ScreenLarge>()->ApplyVanillaAssets(TSAssetPaths::IconLargeBillboard);

	GetMutableDefault<UTSRecipe_ScreenSmall>()->ApplyIngredients(3, 4, 1);
	GetMutableDefault<UTSRecipe_ScreenLarge>()->ApplyIngredients(12, 20, 5);

	GetMutableDefault<UTSSchematic_Screens>()->ApplyVanillaAssets();

	UE_LOG(LogTelescreen, Log, TEXT("Telescreen content resolved"));
}

UTSDesc_Screen::UTSDesc_Screen()
{
	mStackSize = EStackSize::SS_MEDIUM;
}

void UTSDesc_Screen::ApplyVanillaAssets(const TCHAR* IconPath)
{
	if (UTexture2D* Icon = LoadVanillaTexture(IconPath))
	{
		mSmallIcon = Icon;
		mPersistentBigIcon = Icon;
	}
	if (TSubclassOf<UFGCategory> Category = LoadVanillaClass<UFGCategory>(TSAssetPaths::BuildCategoryOrganization))
	{
		mCategory = Category;
	}
	if (TSubclassOf<UFGCategory> SubCategory = LoadVanillaClass<UFGCategory>(TSAssetPaths::BuildSubCategorySigns))
	{
		mSubCategories = {SubCategory};
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: icon %s, category %s, subcategories %d"), *GetClass()->GetName(),
		mSmallIcon ? *mSmallIcon->GetName() : TEXT("none"), mCategory ? *mCategory->GetName() : TEXT("none"), mSubCategories.Num());
}

UTSDesc_ScreenSmall::UTSDesc_ScreenSmall()
{
	mBuildableClass = ATSBuildableScreenSmall::StaticClass();
	mMenuPriority = 10.1f;
}

UTSDesc_ScreenLarge::UTSDesc_ScreenLarge()
{
	mBuildableClass = ATSBuildableScreenLarge::StaticClass();
	mMenuPriority = 10.2f;
}

UTSRecipe_Screen::UTSRecipe_Screen()
{
	mManufactoringDuration = 1.0f;
	mManualManufacturingMultiplier = 1.0f;
	mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TSAssetPaths::BuildGun)));
}

void UTSRecipe_Screen::ApplyIngredients(int32 ReinforcedPlates, int32 CopperSheets, int32 CrystalOscillators)
{
	TSubclassOf<UFGItemDescriptor> Plate = LoadVanillaClass<UFGItemDescriptor>(TSAssetPaths::ItemReinforcedPlate);
	TSubclassOf<UFGItemDescriptor> Sheet = LoadVanillaClass<UFGItemDescriptor>(TSAssetPaths::ItemCopperSheet);
	TSubclassOf<UFGItemDescriptor> Oscillator = LoadVanillaClass<UFGItemDescriptor>(TSAssetPaths::ItemCrystalOscillator);
	if (!Plate || !Sheet || !Oscillator)
	{
		UE_LOG(LogTelescreen, Error, TEXT("%s: ingredients missing; the recipe will be free"), *GetClass()->GetName());
		return;
	}
	mIngredients = {FItemAmount(Plate, ReinforcedPlates), FItemAmount(Sheet, CopperSheets), FItemAmount(Oscillator, CrystalOscillators)};
}

UTSRecipe_ScreenSmall::UTSRecipe_ScreenSmall()
{
	mProduct.Add(FItemAmount(UTSDesc_ScreenSmall::StaticClass(), 1));
}

UTSRecipe_ScreenLarge::UTSRecipe_ScreenLarge()
{
	mProduct.Add(FItemAmount(UTSDesc_ScreenLarge::StaticClass(), 1));
}

UTSUnlockRecipe_Screens::UTSUnlockRecipe_Screens()
{
	mRecipes.Add(UTSRecipe_ScreenSmall::StaticClass());
	mRecipes.Add(UTSRecipe_ScreenLarge::StaticClass());
}

UTSSchematic_Screens::UTSSchematic_Screens()
{
	mType = ESchematicType::EST_ResourceSink;
	mDisplayName = LOCTEXT("SchematicName", "Telescreen Set");
	mDescription = LOCTEXT("SchematicDescription",
		"Small and Large Telescreens: billboard-sized screens that play synced videos, a shared built-in browser, "
		"windows and remote desktops for every player.");
	mMenuPriority = 2.5f;
	mTechTier = 1;
	mTimeToComplete = 0.0f;
	mUnlocks.Add(CreateDefaultSubobject<UTSUnlockRecipe_Screens>(TEXT("UnlockScreens")));
}

void UTSSchematic_Screens::ApplyVanillaAssets()
{
	if (TSubclassOf<UFGItemDescriptor> Coupon = LoadVanillaClass<UFGItemDescriptor>(TSAssetPaths::ItemCoupon))
	{
		mCost = {FItemAmount(Coupon, 25)};
	}
	if (TSubclassOf<UFGSchematicCategory> Category = LoadVanillaClass<UFGSchematicCategory>(TSAssetPaths::ShopCategorySigns))
	{
		mSubCategories = {Category};
	}
	if (UTexture2D* Icon = LoadVanillaTexture(TSAssetPaths::IconLargeBillboard256))
	{
		mSchematicIcon.SetResourceObject(Icon);
		mSchematicIcon.ImageSize = FVector2D(256.0, 256.0);
		mSmallSchematicIcon = Icon;
	}
	UE_LOG(LogTelescreen, Log, TEXT("%s: cost entries %d, shop subcategories %d, unlocks %d"), *GetClass()->GetName(), mCost.Num(),
		mSubCategories.Num(), mUnlocks.Num());
}

#undef LOCTEXT_NAMESPACE
