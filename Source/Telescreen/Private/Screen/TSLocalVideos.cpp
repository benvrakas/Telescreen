#include "Screen/TSLocalVideos.h"
#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Telescreen.h"
#include "TSConfig.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shobjidl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	// Most files indexed in one videos folder.
	constexpr int32 MaxIndexedFiles = 200000;
	// A miss re-indexes the folder at most this often (files are added while the game runs).
	constexpr double ReindexSeconds = 30.0;

	struct FVideoIndex
	{
		FCriticalSection Lock;
		FString Folder;
		// Lower-case file name -> full path (the first one found).
		TMap<FString, FString> ByName;
		double BuiltAt = -1.0e9;
		bool bBuilding = false;
		bool bBuilt = false;
	};
	FVideoIndex GVideoIndex;

	void StartVideoIndex(const FString& Folder)
	{
		{
			FScopeLock Guard(&GVideoIndex.Lock);
			if (GVideoIndex.bBuilding)
			{
				return;
			}
			GVideoIndex.bBuilding = true;
		}
		Async(EAsyncExecution::ThreadPool, [Folder]()
		{
			const double Start = FPlatformTime::Seconds();
			TMap<FString, FString> ByName;
			int32 Count = 0;
			IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
			Files.IterateDirectoryRecursively(*Folder, [&ByName, &Count](const TCHAR* Path, bool bIsDirectory)
			{
				if (!bIsDirectory)
				{
					ByName.FindOrAdd(FPaths::GetCleanFilename(Path).ToLower(), Path);
					++Count;
				}
				return Count < MaxIndexedFiles;
			});
			UE_LOG(LogTelescreen, Log, TEXT("Videos folder %s: %d files indexed in %.1f s%s"), *Folder, Count, FPlatformTime::Seconds() - Start,
				Count >= MaxIndexedFiles ? TEXT(" (limit reached)") : TEXT(""));
			FScopeLock Guard(&GVideoIndex.Lock);
			GVideoIndex.Folder = Folder;
			GVideoIndex.ByName = MoveTemp(ByName);
			GVideoIndex.BuiltAt = FPlatformTime::Seconds();
			GVideoIndex.bBuilding = false;
			GVideoIndex.bBuilt = true;
		});
	}

#if PLATFORM_WINDOWS
	void* GetVideoPickerParent()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return nullptr;
		}
		const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
		return Window.IsValid() && Window->GetNativeWindow().IsValid() ? Window->GetNativeWindow()->GetOSWindowHandle() : nullptr;
	}

	// Runs the Windows picker on its own thread, so the game keeps running (and a listen server keeps serving).
	void PickWithDialog(bool bFolder, const FString& Start, TFunction<void(const FString&)> OnPicked)
	{
		void* Parent = GetVideoPickerParent();
		Async(EAsyncExecution::Thread, [bFolder, Start, Parent, OnPicked = MoveTemp(OnPicked)]()
		{
			FString Result;
			const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			IFileOpenDialog* Dialog = nullptr;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
			{
				DWORD Options = 0;
				Dialog->GetOptions(&Options);
				Dialog->SetOptions(Options | FOS_FORCEFILESYSTEM | (bFolder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
				Dialog->SetTitle(bFolder ? TEXT("Telescreen: choose your videos folder") : TEXT("Telescreen: choose a video"));
				if (!bFolder)
				{
					const COMDLG_FILTERSPEC Types[] = {
						{TEXT("Videos"), TEXT("*.mp4;*.mkv;*.webm;*.mov;*.avi;*.m4v;*.wmv;*.flv;*.ts;*.m2ts;*.mpg;*.mpeg;*.ogv;*.mp3;*.flac;*.ogg;*.wav;*.m4a")},
						{TEXT("All files"), TEXT("*.*")}};
					Dialog->SetFileTypes(UE_ARRAY_COUNT(Types), Types);
				}
				IShellItem* Folder = nullptr;
				if (!Start.IsEmpty() && SUCCEEDED(SHCreateItemFromParsingName(*FPaths::ConvertRelativePathToFull(Start).Replace(TEXT("/"), TEXT("\\")), nullptr,
					IID_PPV_ARGS(&Folder))))
				{
					Dialog->SetFolder(Folder);
					Folder->Release();
				}
				if (SUCCEEDED(Dialog->Show(static_cast<HWND>(Parent))))
				{
					IShellItem* Item = nullptr;
					if (SUCCEEDED(Dialog->GetResult(&Item)))
					{
						PWSTR Path = nullptr;
						if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Path)))
						{
							Result = Path;
							CoTaskMemFree(Path);
						}
						Item->Release();
					}
				}
				Dialog->Release();
			}
			else
			{
				UE_LOG(LogTelescreen, Warning, TEXT("Videos: the Windows file picker couldn't be opened"));
			}
			if (SUCCEEDED(Init))
			{
				CoUninitialize();
			}
			AsyncTask(ENamedThreads::GameThread, [OnPicked, Result]() { OnPicked(Result); });
		});
	}
#endif
}

bool TSLocalVideos::IsLocalFile(const FString& Url)
{
	return !Url.Contains(TEXT("://"));
}

FString TSLocalVideos::GetFolder(const UObject* Context)
{
	FString Folder = UTSConfig::GetString(Context, UTSConfig::VideosFolderKey, FString()).TrimStartAndEnd().TrimQuotes();
	FPaths::NormalizeDirectoryName(Folder);
	return Folder;
}

void TSLocalVideos::SetFolder(const UObject* Context, const FString& Folder)
{
	UTSConfig::SetString(Context, UTSConfig::VideosFolderKey, Folder);
	FScopeLock Guard(&GVideoIndex.Lock);
	GVideoIndex.bBuilt = false;
	GVideoIndex.BuiltAt = -1.0e9;
	UE_LOG(LogTelescreen, Log, TEXT("Videos folder set to %s"), *Folder);
}

TSLocalVideos::EResolve TSLocalVideos::Resolve(const UObject* Context, const FString& File, FString& OutPath)
{
	const FString Path = File.TrimStartAndEnd().TrimQuotes();
	if (!Path.IsEmpty() && IFileManager::Get().FileExists(*Path))
	{
		OutPath = Path;
		return EResolve::Found;
	}
	const FString Folder = GetFolder(Context);
	if (Folder.IsEmpty() || !IFileManager::Get().DirectoryExists(*Folder))
	{
		return EResolve::Missing;
	}
	const FString Name = FPaths::GetCleanFilename(Path.Replace(TEXT("\\"), TEXT("/"))).ToLower();
	bool bStale = false;
	{
		FScopeLock Guard(&GVideoIndex.Lock);
		if (GVideoIndex.bBuilt && GVideoIndex.Folder == Folder)
		{
			if (const FString* Found = GVideoIndex.ByName.Find(Name); Found && IFileManager::Get().FileExists(**Found))
			{
				OutPath = *Found;
				return EResolve::Found;
			}
			bStale = FPlatformTime::Seconds() - GVideoIndex.BuiltAt > ReindexSeconds;
			if (!bStale && !GVideoIndex.bBuilding)
			{
				return EResolve::Missing;
			}
		}
	}
	StartVideoIndex(Folder);
	return EResolve::Searching;
}

void TSLocalVideos::PickFolder(const FString& Start, TFunction<void(const FString&)> OnPicked)
{
#if PLATFORM_WINDOWS
	PickWithDialog(true, Start, MoveTemp(OnPicked));
#else
	OnPicked(FString());
#endif
}

void TSLocalVideos::PickFile(const FString& Start, TFunction<void(const FString&)> OnPicked)
{
#if PLATFORM_WINDOWS
	PickWithDialog(false, Start, MoveTemp(OnPicked));
#else
	OnPicked(FString());
#endif
}

void TSLocalVideos::OpenFolder(const UObject* Context)
{
	const FString Folder = GetFolder(Context);
	if (!Folder.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*Folder);
	}
}
