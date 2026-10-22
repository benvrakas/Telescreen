#include "Runtime/TSRuntimeComponents.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Telescreen.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"

#endif

#define LOCTEXT_NAMESPACE "Telescreen"

const TCHAR* FTSRuntimeComponents::Host = TEXT("host");
const TCHAR* FTSRuntimeComponents::Fetcher = TEXT("fetcher");
const TCHAR* FTSRuntimeComponents::Browser = TEXT("browser");

namespace
{
	FString HashCachePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("RuntimeHashCache.json"));
	}

	// SHA-256 of a file as lowercase hex, or empty on failure.
	FString HashFile(const FString& Path)
	{
#if PLATFORM_WINDOWS
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (!Reader)
		{
			return FString();
		}
		BCRYPT_ALG_HANDLE Algorithm = nullptr;
		BCRYPT_HASH_HANDLE Hash = nullptr;
		if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
		{
			return FString();
		}
		FString Result;
		if (BCryptCreateHash(Algorithm, &Hash, nullptr, 0, nullptr, 0, 0) == 0)
		{
			TArray<uint8> Buffer;
			Buffer.SetNumUninitialized(1 << 20);
			const int64 Total = Reader->TotalSize();
			bool bOk = true;
			for (int64 Done = 0; Done < Total && bOk;)
			{
				const int64 Chunk = FMath::Min<int64>(Buffer.Num(), Total - Done);
				Reader->Serialize(Buffer.GetData(), Chunk);
				bOk = !Reader->IsError() && BCryptHashData(Hash, Buffer.GetData(), ULONG(Chunk), 0) == 0;
				Done += Chunk;
			}
			uint8 Digest[32];
			if (bOk && BCryptFinishHash(Hash, Digest, sizeof(Digest), 0) == 0)
			{
				Result = BytesToHex(Digest, sizeof(Digest)).ToLower();
			}
			BCryptDestroyHash(Hash);
		}
		BCryptCloseAlgorithmProvider(Algorithm, 0);
		return Result;
#else
		return FString();
#endif
	}
}

FTSRuntimeComponents& FTSRuntimeComponents::Get()
{
	static FTSRuntimeComponents Instance;
	return Instance;
}

FTSRuntimeComponents::FTSRuntimeComponents()
{
#define TS_RUNTIME_COMPONENT(Id, Version) Components.Add({Id, Version});
#define TS_RUNTIME_FILE(Id, Path, Size, Sha) Components.Last().Files.Add({Path, Size, Sha});
#include "Runtime/TSRuntimeManifest.inl"
#undef TS_RUNTIME_COMPONENT
#undef TS_RUNTIME_FILE
}

void FTSRuntimeComponents::StartVerification()
{
	if (bStarted.exchange(true))
	{
		return;
	}
#if PLATFORM_WINDOWS
	Async(EAsyncExecution::Thread, [this] { Verify(); });
#else
	{
		FScopeLock ScopeLock(&Lock);
		for (FComponent& Component : Components) Component.Problem = TEXT("not supported on this platform");
	}
	bReady = true;
#endif
}

void FTSRuntimeComponents::Verify()
{
	const double Start = FPlatformTime::Seconds();
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Telescreen"));
	const FString BundledRoot = Plugin ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Runtime")) : FString();
	const FString DownloadRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Telescreen") / TEXT("Runtime"));

	// Cache of hashes keyed by "path|size|timestamp", so unchanged files aren't hashed again on every start.
	TMap<FString, FString> Cache;
	FString CacheText;
	if (FFileHelper::LoadFileToString(CacheText, *HashCachePath()))
	{
		TSharedPtr<FJsonObject> Json;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CacheText), Json) && Json.IsValid())
		{
			for (const auto& Pair : Json->Values) Cache.Add(Pair.Key, Pair.Value->AsString());
		}
	}
	bool bCacheChanged = false;

	TArray<FComponent> Working;
	{
		FScopeLock ScopeLock(&Lock);
		Working = Components;
	}
	for (FComponent& Component : Working)
	{
		const FString Bundled = BundledRoot.IsEmpty() ? FString() : BundledRoot / Component.Id;
		const FString Downloaded = DownloadRoot / (Component.Id + TEXT("-") + Component.Version);
		FString BundledProblem;
		if (!Bundled.IsEmpty() && VerifyIn(Component, Bundled, Cache, bCacheChanged))
		{
			continue;
		}
		BundledProblem = Component.Problem;
		if (VerifyIn(Component, Downloaded, Cache, bCacheChanged))
		{
			continue;
		}
		if (!BundledProblem.IsEmpty() && !BundledProblem.StartsWith(TEXT("missing")))
		{
			Component.Problem = BundledProblem;
		}
		UE_LOG(LogTelescreen, Warning, TEXT("Runtime component '%s' %s unavailable: %s"), *Component.Id, *Component.Version, *Component.Problem);
	}

	if (bCacheChanged)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		for (const auto& Pair : Cache) Json->SetStringField(Pair.Key, Pair.Value);
		FString Out;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Out));
		FFileHelper::SaveStringToFile(Out, *HashCachePath());
	}

	{
		FScopeLock ScopeLock(&Lock);
		Components = Working;
	}
	bReady = true;
	UE_LOG(LogTelescreen, Log, TEXT("Runtime components verified in %.2f s: host %s, fetcher %s, browser %s"), FPlatformTime::Seconds() - Start,
		IsAvailable(Host) ? TEXT("ok") : TEXT("unavailable"), IsAvailable(Fetcher) ? TEXT("ok") : TEXT("unavailable"),
		IsAvailable(Browser) ? TEXT("ok") : TEXT("unavailable"));
}

bool FTSRuntimeComponents::VerifyIn(FComponent& Component, const FString& Dir, TMap<FString, FString>& Cache, bool& bCacheChanged)
{
	IFileManager& Files = IFileManager::Get();
	for (const FFileEntry& Entry : Component.Files)
	{
		const FString Path = Dir / Entry.RelativePath;
		const int64 Size = Files.FileSize(*Path);
		if (Size < 0)
		{
			Component.Problem = FString::Printf(TEXT("missing %s"), *Path);
			return false;
		}
		if (Size != Entry.Size)
		{
			Component.Problem = FString::Printf(TEXT("%s has the wrong size (%lld, expected %lld)"), *Path, Size, Entry.Size);
			return false;
		}
		const FString Key = FString::Printf(TEXT("%s|%lld|%lld"), *Path, Size, Files.GetTimeStamp(*Path).GetTicks());
		FString Sha = Cache.FindRef(Key);
		if (Sha.IsEmpty())
		{
			Sha = HashFile(Path);
			Cache.Add(Key, Sha);
			bCacheChanged = true;
		}
		if (Sha != Entry.Sha256)
		{
			Component.Problem = FString::Printf(TEXT("%s does not match its checksum"), *Path);
			Cache.Remove(Key);
			return false;
		}
	}
	Component.ResolvedDir = Dir;
	Component.Problem.Reset();
	UE_LOG(LogTelescreen, Log, TEXT("Runtime component '%s' %s found in %s"), *Component.Id, *Component.Version, *Dir);
	return true;
}

const FTSRuntimeComponents::FComponent* FTSRuntimeComponents::Find(const TCHAR* Id) const
{
	return Components.FindByPredicate([Id](const FComponent& C) { return C.Id == Id; });
}

bool FTSRuntimeComponents::IsAvailable(const TCHAR* Component) const
{
	if (!bReady)
	{
		return false;
	}
	FScopeLock ScopeLock(&Lock);
	const FComponent* Found = Find(Component);
	return Found && !Found->ResolvedDir.IsEmpty();
}

FString FTSRuntimeComponents::GetFilePath(const TCHAR* Component, const TCHAR* File) const
{
	if (!bReady)
	{
		return FString();
	}
	FScopeLock ScopeLock(&Lock);
	const FComponent* Found = Find(Component);
	return Found && !Found->ResolvedDir.IsEmpty() ? Found->ResolvedDir / File : FString();
}

FText FTSRuntimeComponents::GetUnavailableReason(const TCHAR* Component) const
{
	if (!bReady)
	{
		return LOCTEXT("ComponentsChecking", "Checking Telescreen's components...");
	}
	FScopeLock ScopeLock(&Lock);
	const FComponent* Found = Find(Component);
	if (!Found)
	{
		return LOCTEXT("ComponentUnknown", "Unknown component");
	}
	return FText::Format(LOCTEXT("ComponentUnavailable", "Telescreen component '{0}' is not installed correctly: {1}"), FText::FromString(Found->Id),
		FText::FromString(Found->Problem));
}

#undef LOCTEXT_NAMESPACE
