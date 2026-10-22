#include "Buildables/TSBuildableScreen.h"
#include "Components/StaticMeshComponent.h"
#include "Content/TSAssetPaths.h"
#include "Engine/StaticMesh.h"
#include "FGColoredInstanceMeshProxy.h"
#include "Hologram/FGHologram.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Screen/TSTestPattern.h"
#include "Telescreen.h"

#define LOCTEXT_NAMESPACE "Telescreen"

namespace
{
	const FName ScreenTextureParam(TEXT("SlateUI"));
	const FName ScreenEmissionParam(TEXT("Emission"));
	const FName ScreenGlossinessParam(TEXT("Glossiness"));
	const FName ScreenTintParam(TEXT("TintColorAndOpacity"));
	const FName ScreenBackgroundParam(TEXT("Background"));
	const FName ScreenOpacityFromTextureParam(TEXT("OpacityFromTexture"));
	constexpr int32 TestPatternVariants = 3;
}

ATSBuildableScreen::ATSBuildableScreen()
{
	mScreenPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenPlane"));
	mScreenPlane->SetupAttachment(RootComponent);
	mScreenPlane->SetMobility(EComponentMobility::Static);
	mScreenPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	mScreenPlane->SetCastShadow(false);
	mScreenPlaneSize = FVector2D(100.0, 100.0);
	mAllowColoring = false;
}

void ATSBuildableScreen::ConfigureBillboardLayout(const FVector& MeshLocation, float MeshScale, const FVector& PlaneLocation,
	const FVector2D& PlaneSize, const FVector2D& WorldDimensions, const FVector& PoleOffset, const FVector2D& PoleScale, float SignToSignOffset, const FBox& ClearanceBox,
	const FVector& ClearanceOffset)
{
	mMeshComponentProxy->SetRelativeLocation(MeshLocation);
	mMeshComponentProxy->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
	mMeshComponentProxy->SetRelativeScale3D(FVector(MeshScale));

	// The engine plane is 100 x 100 units; roll 90 stands it up facing the sign's front, as the vanilla ProxyMesh.
	mScreenPlane->SetRelativeLocation(PlaneLocation);
	mScreenPlane->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));
	mScreenPlane->SetRelativeScale3D(FVector(PlaneSize.X / 100.0, PlaneSize.Y / 100.0, 1.0));
	mScreenPlaneSize = PlaneSize;

	mWorldDimensions = WorldDimensions;
	mPoleOffset = PoleOffset;
	mPoleScale = PoleScale;
	mSignToSignOffset = SignToSignOffset;

	FFGClearanceData Clearance;
	Clearance.Type = EClearanceType::CT_Soft;
	Clearance.ClearanceBox = ClearanceBox;
	Clearance.RelativeTransform = FTransform(ClearanceOffset);
	mClearanceData = {Clearance};
}

void ATSBuildableScreen::ResolveVanillaAssets()
{
	GetMutableDefault<ATSBuildableScreenSmall>()->ApplyVanillaAssets(TSAssetPaths::HologramSmallBillboard);
	GetMutableDefault<ATSBuildableScreenLarge>()->ApplyVanillaAssets(TSAssetPaths::HologramLargeBillboard);
}

void ATSBuildableScreen::ApplyVanillaAssets(const TCHAR* HologramPath)
{
	UStaticMesh* BillboardMesh = LoadObject<UStaticMesh>(nullptr, TSAssetPaths::BillboardMesh);
	UMaterialInterface* Background = LoadObject<UMaterialInterface>(nullptr, TSAssetPaths::SignBackgroundMaterial);
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TSAssetPaths::EnginePlane);
	UMaterialInterface* ScreenMaterial = LoadObject<UMaterialInterface>(nullptr, TSAssetPaths::SignWidgetMaterial);
	UClass* Hologram = LoadClass<AFGHologram>(nullptr, HologramPath);

	if (BillboardMesh)
	{
		if (!mMeshComponentProxy->SetStaticMesh(BillboardMesh))
		{
			UE_LOG(LogTelescreen, Error, TEXT("%s: SetStaticMesh refused the billboard mesh on the class default"), *GetClass()->GetName());
		}
		if (Background)
		{
			mMeshComponentProxy->SetMaterial(1, Background);
		}
	}
	if (Plane && !mScreenPlane->SetStaticMesh(Plane))
	{
		UE_LOG(LogTelescreen, Error, TEXT("%s: SetStaticMesh refused the screen plane on the class default"), *GetClass()->GetName());
	}
	if (ScreenMaterial)
	{
		mScreenMaterial = ScreenMaterial;
	}
	if (Hologram)
	{
		mHologramClass = Hologram;
	}

	UE_LOG(LogTelescreen, Log, TEXT("%s: billboard mesh %s, background %s, plane %s, screen material %s, hologram %s"), *GetClass()->GetName(),
		BillboardMesh ? TEXT("ok") : TEXT("MISSING"), Background ? TEXT("ok") : TEXT("MISSING"), Plane ? TEXT("ok") : TEXT("MISSING"),
		ScreenMaterial ? TEXT("ok") : TEXT("MISSING"), Hologram ? *Hologram->GetName() : TEXT("MISSING"));
}

void ATSBuildableScreen::BeginPlay()
{
	Super::BeginPlay();

	const FVector2D Size = GetScreenWorldSize();
	UE_LOG(LogTelescreen, Log, TEXT("%s began play at %s, screen %.0f x %.0f cm"), *GetName(), *GetActorLocation().ToCompactString(), Size.X,
		Size.Y);

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!mScreenMaterial)
	{
		UE_LOG(LogTelescreen, Error, TEXT("%s: no screen material; the face stays blank"), *GetName());
		return;
	}
	mScreenMID = UMaterialInstanceDynamic::Create(mScreenMaterial, this);
	mScreenMID->SetScalarParameterValue(ScreenEmissionParam, 1.0f);
	mScreenMID->SetScalarParameterValue(ScreenGlossinessParam, 0.0f);
	mScreenMID->SetScalarParameterValue(ScreenOpacityFromTextureParam, 0.0f);
	mScreenMID->SetVectorParameterValue(ScreenTintParam, FLinearColor::White);
	mScreenMID->SetVectorParameterValue(ScreenBackgroundParam, FLinearColor::Black);
	mScreenPlane->SetMaterial(0, mScreenMID);
	SetScreenTexture(nullptr);
}

void ATSBuildableScreen::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTelescreen, Verbose, TEXT("%s end play (%d)"), *GetName(), (int32)EndPlayReason);
	Super::EndPlay(EndPlayReason);
}

void ATSBuildableScreen::SetScreenTexture(UTexture* Texture)
{
	if (!mScreenMID)
	{
		return;
	}
	UTexture* Shown = Texture ? Texture : TSTestPattern::Get(mTestPatternVariant);
	if (!Shown)
	{
		UE_LOG(LogTelescreen, Warning, TEXT("%s: no texture to show"), *GetName());
		return;
	}
	mScreenMID->SetTextureParameterValue(ScreenTextureParam, Shown);
}

FVector2D ATSBuildableScreen::GetScreenWorldSize() const
{
	const FVector Scale = mScreenPlane->GetComponentScale();
	return FVector2D(100.0 * FMath::Abs(Scale.X), 100.0 * FMath::Abs(Scale.Y));
}

bool ATSBuildableScreen::IsUseable_Implementation() const
{
	return true;
}

void ATSBuildableScreen::OnUse_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State)
{
	mTestPatternVariant = (mTestPatternVariant + 1) % TestPatternVariants;
	UE_LOG(LogTelescreen, Log, TEXT("%s used; test pattern %d"), *GetName(), mTestPatternVariant);
	SetScreenTexture(nullptr);
}

FText ATSBuildableScreen::GetLookAtDecription_Implementation(AFGCharacterPlayer* ByCharacter, const FUseState& State) const
{
	return FText::Format(LOCTEXT("LookAt", "{0}: change test pattern"), mDisplayName);
}

ATSBuildableScreenSmall::ATSBuildableScreenSmall()
{
	mDisplayName = LOCTEXT("SmallName", "Small Telescreen");
	mDescription = LOCTEXT("SmallDescription",
		"A screen for synced videos, a shared built-in browser, windows and remote desktops, seen by every player.\n\n"
		"Can be freestanding, placed on Walls, or attached to most buildings.\n\nSize: 8 m x 4 m");
	ConfigureBillboardLayout(FVector(0.0, -18.0, 0.0), 1.143f, FVector(0.0, 10.0, 0.0), FVector2D(795.0, 395.0), FVector2D(800.0, 400.0),
		FVector(0.0, 38.0, 0.0),
		FVector2D(1.0, 1.0), 0.0f, FBox(FVector(-400.0, -10.0, -200.0), FVector(400.0, 10.0, 200.0)), FVector(0.0, -2.0, 0.0));
}

ATSBuildableScreenLarge::ATSBuildableScreenLarge()
{
	mDisplayName = LOCTEXT("LargeName", "Large Telescreen");
	mDescription = LOCTEXT("LargeDescription",
		"A screen for synced videos, a shared built-in browser, windows and remote desktops, seen by every player.\n\n"
		"Can be freestanding, placed on Walls, or attached to most buildings.\n\nSize: 16 m x 8 m");
	ConfigureBillboardLayout(FVector(0.0, 15.0, 0.0), 2.29f, FVector(0.0, 66.719, 0.0), FVector2D(1600.0, 800.0), FVector2D(1600.0, 800.0),
		FVector(0.0, 25.0, 0.0),
		FVector2D(2.0, 2.0), 60.0f, FBox(FVector(-800.0, -20.0, -400.0), FVector(800.0, 20.0, 400.0)), FVector(0.0, 46.0, 0.0));
}

#undef LOCTEXT_NAMESPACE
