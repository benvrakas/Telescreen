#pragma once

#include "Common.h"
#include "Session.h"
#include "SharedMemory.h"

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// An off-screen CEF browser session with tabs: each tab is its own CEF browser; only the active one paints, is heard
// and takes input (the others are hidden and muted). Its frames and audio go to the session's shared buffers.
class FBrowserSession : public FSession
{
public:
	FBrowserSession(const FBrowserParams& Params, FSessionEvents Events);
	virtual ~FBrowserSession() override;

	virtual void HandleInput(TSHostProtocol::EInput Kind, FPayloadReader& Reader) override;
	virtual void HandleCommand(TSHostProtocol::ECommand Command, FPayloadReader& Reader) override;

	uint64_t GetFramesPublished() const { return FramesPublished.load(); }

	// Called by the CEF clients, with the tab they belong to.
	void OnTabCreated(uint32_t Tab, CefRefPtr<CefBrowser> Browser);
	void OnPaint(uint32_t Tab, bool bPopup, const void* Buffer, int Width, int Height);
	void OnPopupShow(uint32_t Tab, bool bShow);
	void OnPopupRect(uint32_t Tab, int X, int Y, int Width, int Height);
	void OnAudioStarted(uint32_t Tab, int Channels);
	void OnAudioPacket(uint32_t Tab, const float** Data, int Frames);
	void OnBrowserState(uint32_t Tab, const std::string& Url, const std::string& Title, int Loading, int CanGoBack, int CanGoForward, int Cursor,
		bool PointerLocked);
	// A page asked for a new window or tab (a target=_blank link, window.open, a middle click); false if no tab can open.
	bool OnPageOpensTab(const std::string& Url, bool bForeground);
	void GetViewSize(int& Width, int& Height) const;

	// Most tabs one session keeps open; further pop-ups load in the current tab.
	static constexpr size_t MaxTabs = 12;

private:
	struct FTab
	{
		uint32_t Id = 0;
		CefRefPtr<FBrowserClient> Client;
		CefRefPtr<CefBrowser> Browser;
		std::vector<uint8_t> View;
		int ViewWidth = 0, ViewHeight = 0;
		std::vector<uint8_t> Popup;
		int PopupX = 0, PopupY = 0, PopupWidth = 0, PopupHeight = 0;
		bool PopupVisible = false;
		std::string Url, Title;
		int Loading = 0, CanGoBack = 0, CanGoForward = 0, Cursor = 0;
		bool PointerLocked = false;
	};

	// Starts a tab's CEF browser at Url; returns its id. Caller holds TabsMutex.
	uint32_t AddTabLocked(const std::string& Url, bool bActivate);
	// Shows Tab and hides the others. Caller holds TabsMutex.
	void ActivateLocked(uint32_t Tab);
	FTab* FindTabLocked(uint32_t Tab);
	CefRefPtr<CefBrowser> GetActiveBrowser();
	void Publish(FTab& Tab);
	void SendStateLocked(const FTab& Tab);
	void SendTabsLocked();

	FBrowserParams Params;
	FSessionEvents Events;
	std::wstring CachePath;
	// The session's request context, made on the CEF UI thread by its first tab.
	std::shared_ptr<CefRefPtr<CefRequestContext>> Context;

	std::mutex TabsMutex;
	std::vector<std::unique_ptr<FTab>> Tabs;
	uint32_t ActiveTab = 0;
	uint32_t NextTabId = 1;
	std::vector<uint8_t> Composed;
	std::unique_ptr<FVideoWriter> VideoWriter;
	uint32_t VideoGeneration = 0;
	std::atomic<uint64_t> FramesPublished{0};

	std::mutex AudioMutex;
	std::unique_ptr<FAudioWriter> AudioWriter;
	int AudioChannels = 2;
	std::vector<float> Interleaved;

	std::atomic<int> Width{1280}, Height{720};
};
