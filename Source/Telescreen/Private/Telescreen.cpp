#include "Telescreen.h"

DEFINE_LOG_CATEGORY(LogTelescreen);

void FTelescreenModule::StartupModule()
{
	UE_LOG(LogTelescreen, Log, TEXT("Telescreen module starting"));
}

void FTelescreenModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FTelescreenModule, Telescreen)
