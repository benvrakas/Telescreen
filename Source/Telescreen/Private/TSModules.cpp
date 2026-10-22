#include "TSModules.h"
#include "Content/TSContent.h"
#include "Telescreen.h"

UTSGameInstanceModule::UTSGameInstanceModule()
{
	bRootModule = true;
}

void UTSGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		TSContent::ResolveVanillaAssets();
	}
	Super::DispatchLifecycleEvent(Phase);
}

UTSGameWorldModule::UTSGameWorldModule()
{
	bRootModule = true;
	mSchematics.Add(UTSSchematic_Screens::StaticClass());
}
