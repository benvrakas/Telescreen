#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"

#include <atomic>
#include <memory>
#include <mutex>

class FBrowserClient;

namespace Browser
{
	// Runs this process as a CEF subprocess if the command line says so; returns its exit code, or -1 in the main
	// process. Must be the first thing wmain does.
	int RunSubprocessIfNeeded();

	// Starts CEF in this (main) process on first use; BrowserDir holds libcef.dll and its resources. Main thread only.
	bool EnsureInitialized(const std::wstring& BrowserDir, const std::wstring& DataDir, const std::wstring& LogDir, std::string& Error);

	// Stops CEF if it was started. Main thread only, after every browser session is gone.
	void Shutdown();

	// Lets file:// pages load (selftest only).
	void AllowFileUrls(bool bAllow);
}

struct FBrowserParams
{
	uint32_t SessionId = 0;
	std::string Url;
	uint32_t Width = 1280;
	uint32_t Height = 720;
	bool KeepLogins = false;
	std::string ProfileName;
};

// An off-screen CEF browser whose frames and audio go to a session's shared buffers.
class FBrowserSession : public FSession
{
public:
	FBrowserSession(const FBrowserParams& Params, FSessionEvents Events);
	virtual ~FBrowserSession() override;

	virtual void HandleInput(TSHostProtocol::EInput Kind, FPayloadReader& Reader) override;
	virtual void HandleCommand(TSHostProtocol::ECommand Command, FPayloadReader& Reader) override;

	uint64_t GetFramesPublished() const { return FramesPublished.load(); }

	// Called by the CEF client.
	void OnPaint(bool bPopup, const void* Buffer, int Width, int Height);
	void OnPopupShow(bool bShow);
	void OnPopupRect(int X, int Y, int Width, int Height);
	void OnAudioStarted(int Channels);
	void OnAudioPacket(const float** Data, int Frames);
	void OnBrowserState(const std::string& Url, const std::string& Title, int Loading, int CanGoBack, int CanGoForward, int Cursor, bool PointerLocked);
	void OnClosed();
	void GetViewSize(int& Width, int& Height) const;

private:
	void Publish();

	FBrowserParams Params;
	FSessionEvents Events;
	FBrowserClient* Client = nullptr;

	std::mutex PaintMutex;
	std::vector<uint8_t> View;
	int ViewWidth = 0, ViewHeight = 0;
	std::vector<uint8_t> Popup;
	int PopupX = 0, PopupY = 0, PopupWidth = 0, PopupHeight = 0;
	bool PopupVisible = false;
	std::vector<uint8_t> Composed;
	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;
	std::atomic<uint64_t> FramesPublished{0};

	std::mutex AudioMutex;
	std::unique_ptr<FAudioWriter> AudioWriter;
	int AudioChannels = 2;
	std::vector<float> Interleaved;

	std::mutex StateMutex;
	std::string Url, Title;
	int Loading = 0, CanGoBack = 0, CanGoForward = 0, Cursor = 0;

	std::atomic<int> Width{1280}, Height{720};
	HANDLE ClosedEvent = nullptr;
};
