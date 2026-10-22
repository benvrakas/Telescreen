#include "Telescreen.h"
#include "Audio/TSWwiseScreens.h"

DEFINE_LOG_CATEGORY(LogTelescreen);

void FTelescreenModule::StartupModule()
{
	UE_LOG(LogTelescreen, Log, TEXT("Telescreen module starting"));
}

void FTelescreenModule::ShutdownModule()
{
	TSWwiseScreens::Shutdown();
}

IMPLEMENT_MODULE(FTelescreenModule, Telescreen)
