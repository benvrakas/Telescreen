#include "Screen/TSScreenVolumes.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Telescreen.h"

namespace
{
	TMap<FString, float> Volumes;
	bool bLoaded = false;

	FString VolumesPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("ScreenVolumes.json");
	}

	FString KeyFor(const AActor* Screen)
	{
		const FVector Where = Screen->GetActorLocation();
		return FString::Printf(TEXT("%s@%d,%d,%d"), *Screen->GetName(), FMath::RoundToInt(Where.X / 100.0), FMath::RoundToInt(Where.Y / 100.0),
			FMath::RoundToInt(Where.Z / 100.0));
	}

	void Load()
	{
		if (bLoaded)
		{
			return;
		}
		bLoaded = true;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Json, *VolumesPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			return;
		}
		for (const auto& Pair : Root->Values)
		{
			double Value = 1.0;
			if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value))
			{
				Volumes.Add(Pair.Key, FMath::Clamp(float(Value), 0.f, 2.f));
			}
		}
		UE_LOG(LogTelescreen, Log, TEXT("Screen volumes: %d loaded"), Volumes.Num());
	}
}

float TSScreenVolumes::Get(const AActor* Screen)
{
	if (!Screen)
	{
		return 1.f;
	}
	Load();
	const float* Found = Volumes.Find(KeyFor(Screen));
	return Found ? *Found : 1.f;
}

void TSScreenVolumes::Set(const AActor* Screen, float Volume, bool bSave)
{
	if (!Screen)
	{
		return;
	}
	Load();
	Volumes.Add(KeyFor(Screen), FMath::Clamp(Volume, 0.f, 2.f));
	if (!bSave)
	{
		return;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const auto& Pair : Volumes)
	{
		Root->SetNumberField(Pair.Key, Pair.Value);
	}
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	if (!FFileHelper::SaveStringToFile(Json, *VolumesPath()))
	{
		UE_LOG(LogTelescreen, Warning, TEXT("Screen volumes: could not write %s"), *VolumesPath());
	}
}
