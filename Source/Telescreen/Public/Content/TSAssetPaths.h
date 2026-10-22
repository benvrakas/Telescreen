#pragma once

#include "CoreMinimal.h"

// Object paths of the vanilla assets Telescreen reuses (see Docs/ReverseEngineering.md).
namespace TSAssetPaths
{
	inline const TCHAR* BillboardMesh = TEXT("/Game/FactoryGame/Buildable/Factory/SignDigital/Mesh/SM_DigitalBillboard_01.SM_DigitalBillboard_01");
	inline const TCHAR* SignBackgroundMaterial = TEXT("/Game/FactoryGame/Buildable/Factory/SignDigital/Material/MI_SignBackground.MI_SignBackground");
	inline const TCHAR* SignWidgetMaterial = TEXT("/Game/FactoryGame/Buildable/Factory/SignDigital/Material/MM_SignWidgetMaterial.MM_SignWidgetMaterial");
	inline const TCHAR* EnginePlane = TEXT("/Engine/BasicShapes/Plane.Plane");

	inline const TCHAR* HologramSmallBillboard = TEXT("/Game/FactoryGame/Buildable/Factory/-Shared/Holo_StandaloneSign_Large.Holo_StandaloneSign_Large_C");
	inline const TCHAR* HologramLargeBillboard = TEXT("/Game/FactoryGame/Buildable/Factory/-Shared/Holo_StandaloneSign_Huge.Holo_StandaloneSign_Huge_C");

	inline const TCHAR* IconSmallBillboard = TEXT("/Game/FactoryGame/Buildable/Factory/StandaloneSign/UI/IconDesc_SignLarge_512.IconDesc_SignLarge_512");
	inline const TCHAR* IconLargeBillboard = TEXT("/Game/FactoryGame/Buildable/Factory/StandaloneSign/UI/IconDesc_SignHuge_512.IconDesc_SignHuge_512");
	inline const TCHAR* IconLargeBillboard256 = TEXT("/Game/FactoryGame/Buildable/Factory/StandaloneSign/UI/IconDesc_SignHuge_256.IconDesc_SignHuge_256");

	inline const TCHAR* BuildCategoryOrganization = TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C");
	inline const TCHAR* BuildSubCategorySigns = TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Signs.SC_Signs_C");
	inline const TCHAR* ShopCategorySigns = TEXT("/Game/FactoryGame/Schematics/ResourceSinkShopCategories/SC_RSS_Signs.SC_RSS_Signs_C");

	inline const TCHAR* BuildGun = TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C");
	inline const TCHAR* ItemReinforcedPlate = TEXT("/Game/FactoryGame/Resource/Parts/SteelPlateReinforced/Desc_SteelPlateReinforced.Desc_SteelPlateReinforced_C");
	inline const TCHAR* ItemCopperSheet = TEXT("/Game/FactoryGame/Resource/Parts/CopperSheet/Desc_CopperSheet.Desc_CopperSheet_C");
	inline const TCHAR* ItemCrystalOscillator = TEXT("/Game/FactoryGame/Resource/Parts/CrystalOscillator/Desc_CrystalOscillator.Desc_CrystalOscillator_C");
	inline const TCHAR* ItemCoupon = TEXT("/Game/FactoryGame/Resource/Parts/ResourceSinkCoupon/Desc_ResourceSinkCoupon.Desc_ResourceSinkCoupon_C");
}
