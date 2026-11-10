// SFModWwiseGameBanks.h (shared by BoomBoxPlus and Telescreen; keep identical in both)
#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IO/IoChunkId.h"
#include "IO/IoDispatcher.h"
#include "IO/PackageId.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// Reads the game's own Boom Box song banks, which the mods turn into their Audio Input banks. Since the game update of
// 2026-11-04 each bank is bulk data inside its Wwise event asset (/Game/WwiseAudio/Events/.../Play_Boombox_*); before
// that they were loose files (Content/WwiseAudio/Event/.../Play_Boombox_*.bnk). Nothing here calls into Wwise.
namespace SFModWwiseGameBanks
{
	// How many song banks were found where, for the log.
	struct FSearch
	{
		int32 EventAssets = 0;
		int32 UnreadableEventAssets = 0;
		int32 BankFiles = 0;
	};

	// The first whole bank in Data: a BKHD chunk followed by the chunks of a bank file, at least one of them HIRC.
	inline bool ExtractBank(const uint8* Data, int64 Size, TArray<uint8>& OutBank)
	{
		auto ReadU32 = [Data](int64 At) { uint32 Value = 0; FMemory::Memcpy(&Value, Data + At, sizeof(Value)); return Value; };
		auto IsBankChunk = [Data](int64 At)
		{
			for (const char* Tag : { "BKHD", "HIRC", "DIDX", "DATA", "STID", "STMG", "ENVS", "PLAT", "INIT", "FXPR" })
			{
				if (FMemory::Memcmp(Data + At, Tag, 4) == 0)
				{
					return true;
				}
			}
			return false;
		};
		for (int64 Start = 0; Start + 16 <= Size; ++Start)
		{
			if (FMemory::Memcmp(Data + Start, "BKHD", 4) != 0)
			{
				continue;
			}
			// The header holds at least the bank version and id; versions are in the hundreds.
			const uint32 HeaderSize = ReadU32(Start + 4);
			const uint32 Version = ReadU32(Start + 8);
			if (HeaderSize < 8 || HeaderSize > 4096 || Version < 100 || Version > 1000)
			{
				continue;
			}
			int64 End = Start;
			bool bHasHirc = false;
			while (End + 8 <= Size && IsBankChunk(End))
			{
				const int64 ChunkEnd = End + 8 + ReadU32(End + 4);
				if (ChunkEnd > Size)
				{
					break;
				}
				bHasHirc |= FMemory::Memcmp(Data + End, "HIRC", 4) == 0;
				End = ChunkEnd;
			}
			if (bHasHirc)
			{
				OutBank = TArray<uint8>(Data + Start, static_cast<int32>(End - Start));
				return true;
			}
		}
		return false;
	}

	// The serialized data of a cooked package: its .uexp file where the game has one (pak or loose files), otherwise its
	// package chunk in the IoStore container.
	inline bool ReadPackageData(FName PackageName, TArray<uint8>& OutData)
	{
		FString File;
		if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), File, TEXT(".uexp"))
			&& IFileManager::Get().FileExists(*File) && FFileHelper::LoadFileToArray(OutData, *File, FILEREAD_Silent))
		{
			return true;
		}
		if (!FIoDispatcher::IsInitialized())
		{
			return false;
		}
		const FIoChunkId Chunk = CreatePackageDataChunkId(FPackageId::FromName(PackageName));
		FIoDispatcher& Dispatcher = FIoDispatcher::Get();
		if (!Dispatcher.DoesChunkExist(Chunk))
		{
			return false;
		}
		FIoBatch Batch = Dispatcher.NewBatch();
		FIoRequest Request = Batch.Read(Chunk, FIoReadOptions(), IoDispatcherPriority_High);
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool();
		Batch.IssueAndTriggerEvent(Done);
		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);
		const FIoBuffer* Result = Request.GetResult();
		if (!Result || Result->DataSize() == 0 || Result->DataSize() > MAX_int32)
		{
			return false;
		}
		OutData = TArray<uint8>(Result->GetData(), static_cast<int32>(Result->DataSize()));
		return true;
	}

	// A song's bank or event, not the Boom Box's interface sounds or animations.
	inline bool IsSongName(const FString& Name)
	{
		return Name.StartsWith(TEXT("Play_Boombox_")) && !Name.Contains(TEXT("_UI_")) && !Name.Contains(TEXT("Animation"));
	}

	// Package names of the game's Boom Box song events, in name order.
	inline TArray<FName> FindSongEvents()
	{
		TArray<FAssetData> Assets;
		if (IAssetRegistry* Registry = IAssetRegistry::Get())
		{
			Registry->GetAssetsByPath(FName(TEXT("/Game/WwiseAudio/Events")), Assets, true);
		}
		Assets.RemoveAll([](const FAssetData& Asset) { return !IsSongName(Asset.AssetName.ToString()); });
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		TArray<FName> Packages;
		for (const FAssetData& Asset : Assets)
		{
			Packages.AddUnique(Asset.PackageName);
		}
		if (Packages.Num() == 0)
		{
			// Known songs, in case the asset registry doesn't list them.
			Packages.Add(FName(TEXT("/Game/WwiseAudio/Events/Equipment/BoomBox/Goat/Play_Boombox_GSFTS_Desert_Chill")));
			Packages.Add(FName(TEXT("/Game/WwiseAudio/Events/Equipment/BoomBox/Goat/Play_Boombox_GSFTS_Goatshire")));
			Packages.Add(FName(TEXT("/Game/WwiseAudio/Events/Equipment/BoomBox/Sanctum/Play_Boombox_GSTFS_Explorer")));
		}
		return Packages;
	}

	// Hands each Boom Box song bank to Visit (name, bank bytes starting with BKHD) until Visit returns true: first the
	// banks inside the game's event assets, then loose bank files.
	inline FSearch ForEachSongBank(TFunctionRef<bool(const FString& Name, const TArray<uint8>& Bank)> Visit)
	{
		FSearch Search;
		for (const FName& Package : FindSongEvents())
		{
			TArray<uint8> Data;
			TArray<uint8> Bank;
			if (!ReadPackageData(Package, Data) || !ExtractBank(Data.GetData(), Data.Num(), Bank))
			{
				++Search.UnreadableEventAssets;
				continue;
			}
			++Search.EventAssets;
			if (Visit(FPackageName::GetShortName(Package), Bank))
			{
				return Search;
			}
		}
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectContentDir() / TEXT("WwiseAudio") / TEXT("Event")), TEXT("Play_Boombox_*.bnk"), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			const FString Name = FPaths::GetBaseFilename(File);
			TArray<uint8> Bank;
			if (!IsSongName(Name) || !FFileHelper::LoadFileToArray(Bank, *File))
			{
				continue;
			}
			++Search.BankFiles;
			if (Visit(Name, Bank))
			{
				return Search;
			}
		}
		return Search;
	}
}
