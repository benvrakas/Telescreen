#include "TSModules.h"
#include "Content/TSContent.h"
#include "Telescreen.h"
#include "TSConfig.h"
#include "TSRemoteCallObject.h"

UTSGameInstanceModule::UTSGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.Add(UTSRemoteCallObject::StaticClass());
	ModConfigurations.Add(UTSConfig::StaticClass());
}

void UTSGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		// SML registers the configuration during initialization; its editor classes must be in place first.
		UTSConfig::UseSMLEditorClasses();
		TSContent::ResolveVanillaAssets();
	}
	Super::DispatchLifecycleEvent(Phase);
}

UTSGameWorldModule::UTSGameWorldModule()
{
	bRootModule = true;
	mSchematics.Add(UTSSchematic_Screens::StaticClass());
}
