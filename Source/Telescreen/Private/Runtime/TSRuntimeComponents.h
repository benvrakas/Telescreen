#pragma once

#include "CoreMinimal.h"

// Finds and verifies Telescreen's runtime components (helper, FFmpeg, yt-dlp), per D:/SFMod/Tools/RuntimeComponents.md.
// Client only; dedicated servers never need them.
class FTSRuntimeComponents
{
public:
	static FTSRuntimeComponents& Get();

	// Component ids, as in Tools/stage_runtime.py.
	static const TCHAR* Host;
	static const TCHAR* Fetcher;
	static const TCHAR* Browser;

	// Starts verifying every component on a background thread; safe to call more than once.
	void StartVerification();

	// True once verification has finished (successfully or not).
	bool IsReady() const { return bReady.load(); }

	// True if Component was found and every one of its files matched the manifest.
	bool IsAvailable(const TCHAR* Component) const;

	// Absolute path of File inside Component's folder, or empty if the component is unavailable.
	FString GetFilePath(const TCHAR* Component, const TCHAR* File) const;

	// Why Component is unavailable, for the UI and the log.
	FText GetUnavailableReason(const TCHAR* Component) const;

private:
	struct FFileEntry
	{
		FString RelativePath;
		int64 Size;
		FString Sha256;
	};
	struct FComponent
	{
		FString Id;
		FString Version;
		TArray<FFileEntry> Files;
		FString ResolvedDir;
		FString Problem;
	};

	FTSRuntimeComponents();
	void Verify();
	bool VerifyIn(FComponent& Component, const FString& Dir, TMap<FString, FString>& Cache, bool& bCacheChanged);
	const FComponent* Find(const TCHAR* Id) const;

	TArray<FComponent> Components;
	mutable FCriticalSection Lock;
	std::atomic<bool> bStarted{false};
	std::atomic<bool> bReady{false};
};
