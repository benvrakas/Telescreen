#include "BrowserSession.h"
#include "Taps.h"

#include <algorithm>
#include <cstring>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "include/cef_version.h"

using TSHostProtocol::ECommand;
using TSHostProtocol::EInput;
using TSHostProtocol::ESessionState;

namespace
{
	constexpr uint32_t AudioRate = 48000;
	constexpr uint32_t AudioCapacity = AudioRate;
	constexpr int64_t MaxAudioQueued = AudioRate / 4;

	std::wstring GBrowserDir;
	std::wstring GDataDir;
	bool GInitialized = false;
	bool GAllowFileUrls = false;

	std::wstring SwitchValue(const wchar_t* Name)
	{
		const std::wstring CommandLine = GetCommandLineW();
		const std::wstring Key = std::wstring(L"--") + Name + L"=";
		const size_t Start = CommandLine.find(Key);
		if (Start == std::wstring::npos) return {};
		size_t Begin = Start + Key.size();
		const bool bQuoted = Begin < CommandLine.size() && CommandLine[Begin] == L'"';
		if (bQuoted) ++Begin;
		const size_t End = CommandLine.find(bQuoted ? L'"' : L' ', Begin);
		return CommandLine.substr(Begin, End == std::wstring::npos ? std::wstring::npos : End - Begin);
	}

	// Pages may only use these schemes; everything else (file, chrome, devtools, view-source, OS protocol handlers) is
	// refused whoever is in control.
	bool IsAllowedUrl(const std::string& Url)
	{
		std::string Lower = Url;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char C) { return char(tolower(C)); });
		return Lower.rfind("https://", 0) == 0 || Lower.rfind("http://", 0) == 0 || Lower.rfind("about:blank", 0) == 0 ||
			Lower.rfind("data:", 0) == 0 || Lower.rfind("blob:", 0) == 0 || (GAllowFileUrls && Lower.rfind("file://", 0) == 0);
	}

	class FTask : public CefTask
	{
	public:
		explicit FTask(std::function<void()> InWork) : Work(std::move(InWork)) {}
		void Execute() override { Work(); }

	private:
		std::function<void()> Work;
		IMPLEMENT_REFCOUNTING(FTask);
	};

	// Stands in for the Pointer Lock API, which off-screen browsers reject (see Docs/HostProtocol.md).
	const char* PointerLockScript = R"JS((() => {
  if (window.__tsPointerMove) return;
  let locked = null, lastX = 0, lastY = 0;
  const notify = v => console.log('__telescreen:pointerlock:' + (v ? 1 : 0));
  const changed = () => setTimeout(() => document.dispatchEvent(new Event('pointerlockchange')), 0);
  Object.defineProperty(Document.prototype, 'pointerLockElement', { configurable: true, get() { return locked; } });
  Element.prototype.requestPointerLock = function () { locked = this; notify(1); changed(); return Promise.resolve(); };
  Document.prototype.exitPointerLock = function () { if (!locked) return; locked = null; notify(0); changed(); };
  addEventListener('mousemove', e => { if (e.isTrusted) { lastX = e.clientX; lastY = e.clientY; } }, true);
  addEventListener('keydown', e => { if (locked && e.key === 'Escape') document.exitPointerLock(); }, true);
  window.__tsPointerMove = (dx, dy) => {
    if (!locked) return;
    locked.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, cancelable: true, view: window, clientX: lastX, clientY: lastY,
      movementX: dx, movementY: dy }));
  };
})();)JS";
	const char* PointerLockTag = "__telescreen:pointerlock:";

	void RunOnUi(std::function<void()> Work)
	{
		CefPostTask(TID_UI, new FTask(std::move(Work)));
	}

	class FHostApp : public CefApp, public CefBrowserProcessHandler
	{
	public:
		CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

		void OnBeforeCommandLineProcessing(const CefString& ProcessType, CefRefPtr<CefCommandLine> CommandLine) override
		{
			if (ProcessType.empty())
			{
				CommandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
				CommandLine->AppendSwitch("disable-extensions");
			}
		}

		void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> CommandLine) override
		{
			CommandLine->AppendSwitchWithValue("ts-browser-dir", GBrowserDir);
		}

	private:
		IMPLEMENT_REFCOUNTING(FHostApp);
	};
}

// CEF handlers for one tab's browser; forwards everything to its session, tagged with the tab, until the session
// detaches. A browser created after the detach is closed at once.
class FBrowserClient : public CefClient,
					   public CefRenderHandler,
					   public CefAudioHandler,
					   public CefDisplayHandler,
					   public CefLoadHandler,
					   public CefLifeSpanHandler,
					   public CefRequestHandler,
					   public CefPermissionHandler,
					   public CefDialogHandler,
					   public CefJSDialogHandler,
					   public CefContextMenuHandler,
					   public CefDownloadHandler
{
public:
	FBrowserClient(FBrowserSession* InSession, uint32_t InTab) : Session(InSession), Tab(InTab)
	{
		ClosedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	}
	~FBrowserClient() override
	{
		CloseHandle(ClosedEvent);
	}

	void Detach()
	{
		std::lock_guard<std::mutex> Lock(SessionMutex);
		Session = nullptr;
	}

	CefRefPtr<CefBrowser> GetBrowser()
	{
		std::lock_guard<std::mutex> Lock(BrowserMutex);
		return Browser;
	}

	// Signalled once the browser has closed.
	HANDLE GetClosedEvent() const { return ClosedEvent; }

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
	CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
	CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
	CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
	CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

	// Render
	void GetViewRect(CefRefPtr<CefBrowser>, CefRect& Rect) override
	{
		int W = 1280, H = 720;
		WithSession([&](FBrowserSession& S) { S.GetViewSize(W, H); });
		Rect = CefRect(0, 0, std::max(W, 16), std::max(H, 16));
	}
	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType Type, const RectList&, const void* Buffer, int W, int H) override
	{
		WithSession([&](FBrowserSession& S) { S.OnPaint(Tab, Type == PET_POPUP, Buffer, W, H); });
	}
	void OnPopupShow(CefRefPtr<CefBrowser>, bool bShow) override
	{
		WithSession([&](FBrowserSession& S) { S.OnPopupShow(Tab, bShow); });
	}
	void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& Rect) override
	{
		WithSession([&](FBrowserSession& S) { S.OnPopupRect(Tab, Rect.x, Rect.y, Rect.width, Rect.height); });
	}

	// Audio: taken here so it plays from the screen in the game, not through Windows.
	bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& Params) override
	{
		Params.sample_rate = AudioRate;
		Params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
		Params.frames_per_buffer = 480;
		return true;
	}
	void OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters&, int Channels) override
	{
		WithSession([&](FBrowserSession& S) { S.OnAudioStarted(Tab, Channels); });
	}
	void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** Data, int Frames, int64_t) override
	{
		WithSession([&](FBrowserSession& S) { S.OnAudioPacket(Tab, Data, Frames); });
	}
	void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override {}
	void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString& Message) override
	{
		LOG_WARN("browser audio error: %s", Message.ToString().c_str());
	}

	// Display and load state
	void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame, const CefString& InUrl) override
	{
		if (Frame->IsMain())
		{
			Url = InUrl.ToString();
			SendState();
		}
	}
	void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& InTitle) override
	{
		Title = InTitle.ToString();
		SendState();
	}
	bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle, cef_cursor_type_t Type, const CefCursorInfo&) override
	{
		Cursor = int(Type);
		SendState();
		return true;
	}
	bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& Message, const CefString&, int) override
	{
		const std::string Text = Message.ToString();
		if (Text.rfind(PointerLockTag, 0) == 0)
		{
			PointerLocked = Text.back() == '1';
			LOG_INFO("browser: page %s the pointer", PointerLocked ? "locked" : "released");
			SendState();
			return true;
		}
		LOG_INFO("page: %s", Text.substr(0, 300).c_str());
		return true;
	}
	void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame, cef_transition_type_t) override
	{
		Frame->ExecuteJavaScript(PointerLockScript, "", 0);
	}
	void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame, int) override
	{
		Frame->ExecuteJavaScript(PointerLockScript, "", 0);
	}
	void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool bIsLoading, bool bCanGoBack, bool bCanGoForward) override
	{
		Loading = bIsLoading;
		CanGoBack = bCanGoBack;
		CanGoForward = bCanGoForward;
		SendState();
	}

	// Life span: pop-ups and new-tab links become tabs of the session (or load here when no tab can open); CEF never
	// creates a window.
	void OnAfterCreated(CefRefPtr<CefBrowser> InBrowser) override
	{
		{
			std::lock_guard<std::mutex> Lock(BrowserMutex);
			Browser = InBrowser;
		}
		CefRefPtr<CefDictionaryValue> Script = CefDictionaryValue::Create();
		Script->SetString("source", PointerLockScript);
		InBrowser->GetHost()->ExecuteDevToolsMethod(0, "Page.addScriptToEvaluateOnNewDocument", Script);
		bool bAttached = false;
		WithSession([&](FBrowserSession& S) { bAttached = true; S.OnTabCreated(Tab, InBrowser); });
		if (!bAttached) InBrowser->GetHost()->CloseBrowser(true);
	}
	bool OnBeforePopup(CefRefPtr<CefBrowser> InBrowser, CefRefPtr<CefFrame>, int, const CefString& TargetUrl, const CefString&,
		cef_window_open_disposition_t Disposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
		CefRefPtr<CefDictionaryValue>&, bool*) override
	{
		OpenFromPage(InBrowser->GetMainFrame(), TargetUrl.ToString(), Disposition);
		return true;
	}
	void OnBeforeClose(CefRefPtr<CefBrowser>) override
	{
		{
			std::lock_guard<std::mutex> Lock(BrowserMutex);
			Browser = nullptr;
		}
		SetEvent(ClosedEvent);
	}

	// Requests: only allowed schemes.
	bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> Request, bool, bool) override
	{
		const std::string Target = Request->GetURL().ToString();
		if (IsAllowedUrl(Target)) return false;
		LOG_WARN("browser: blocked navigation to %s", Target.substr(0, 200).c_str());
		return true;
	}
	bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame, const CefString& TargetUrl, cef_window_open_disposition_t Disposition,
		bool) override
	{
		OpenFromPage(Frame, TargetUrl.ToString(), Disposition);
		return true;
	}

	// Permissions, dialogs, menus and downloads: all refused or suppressed; in off-screen mode they'd be real windows.
	bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& Origin, uint32_t,
		CefRefPtr<CefMediaAccessCallback> Callback) override
	{
		LOG_INFO("browser: denied camera/microphone to %s", Origin.ToString().c_str());
		Callback->Cancel();
		return true;
	}
	bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString& Origin, uint32_t Permissions,
		CefRefPtr<CefPermissionPromptCallback> Callback) override
	{
		LOG_INFO("browser: denied permissions 0x%x to %s", Permissions, Origin.ToString().c_str());
		Callback->Continue(CEF_PERMISSION_RESULT_DENY);
		return true;
	}
	bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode, const CefString&, const CefString&, const std::vector<CefString>&,
		const std::vector<CefString>&, const std::vector<CefString>&, CefRefPtr<CefFileDialogCallback> Callback) override
	{
		Callback->Cancel();
		return true;
	}
	bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType, const CefString&, const CefString&, CefRefPtr<CefJSDialogCallback>,
		bool& bSuppress) override
	{
		bSuppress = true;
		return false;
	}
	bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool, CefRefPtr<CefJSDialogCallback> Callback) override
	{
		Callback->Continue(true, CefString());
		return true;
	}
	void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> Model) override
	{
		Model->Clear();
	}
	bool CanDownload(CefRefPtr<CefBrowser>, const CefString& DownloadUrl, const CefString&) override
	{
		LOG_INFO("browser: blocked download of %s", DownloadUrl.ToString().substr(0, 200).c_str());
		return false;
	}
	bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>, const CefString&, CefRefPtr<CefBeforeDownloadCallback>) override
	{
		return false;
	}

private:
	template <typename F>
	void WithSession(F&& Work)
	{
		std::lock_guard<std::mutex> Lock(SessionMutex);
		if (Session) Work(*Session);
	}

	void SendState()
	{
		WithSession([&](FBrowserSession& S) { S.OnBrowserState(Tab, Url, Title, Loading, CanGoBack, CanGoForward, Cursor, PointerLocked); });
	}

	// A link or script that wants a new tab or window: a new tab when the session can open one, else this frame.
	void OpenFromPage(CefRefPtr<CefFrame> Frame, const std::string& Target, cef_window_open_disposition_t Disposition)
	{
		if (!IsAllowedUrl(Target))
		{
			LOG_WARN("browser: blocked pop-up to %s", Target.substr(0, 200).c_str());
			return;
		}
		if (Disposition == CEF_WOD_CURRENT_TAB)
		{
			if (Frame) Frame->LoadURL(Target);
			return;
		}
		bool bOpened = false;
		const bool bForeground = Disposition != CEF_WOD_NEW_BACKGROUND_TAB;
		WithSession([&](FBrowserSession& S) { bOpened = S.OnPageOpensTab(Target, bForeground); });
		if (!bOpened && Frame) Frame->LoadURL(Target);
	}

	std::mutex SessionMutex;
	FBrowserSession* Session;
	const uint32_t Tab;
	HANDLE ClosedEvent = nullptr;
	std::mutex BrowserMutex;
	CefRefPtr<CefBrowser> Browser;
	std::string Url, Title;
	int Loading = 0, CanGoBack = 0, CanGoForward = 0, Cursor = 0;
	bool PointerLocked = false;

	IMPLEMENT_REFCOUNTING(FBrowserClient);
};

int Browser::RunSubprocessIfNeeded()
{
	if (SwitchValue(L"type").empty())
	{
		return -1;
	}
	const std::wstring Dir = SwitchValue(L"ts-browser-dir");
	if (!Dir.empty()) SetDllDirectoryW(Dir.c_str());
	CefMainArgs Args(GetModuleHandleW(nullptr));
	return CefExecuteProcess(Args, nullptr, nullptr);
}

bool Browser::EnsureInitialized(const std::wstring& BrowserDir, const std::wstring& DataDir, const std::wstring& LogDir, std::string& Error)
{
	if (GInitialized) return true;
	if (BrowserDir.empty() || GetFileAttributesW((BrowserDir + L"\\libcef.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		Error = "the browser component is not installed";
		return false;
	}
	GBrowserDir = BrowserDir;
	GDataDir = DataDir;
	SetDllDirectoryW(BrowserDir.c_str());

	wchar_t Exe[MAX_PATH];
	GetModuleFileNameW(nullptr, Exe, MAX_PATH);
	CefSettings Settings;
	Settings.no_sandbox = true;
	Settings.multi_threaded_message_loop = true;
	Settings.windowless_rendering_enabled = true;
	Settings.log_severity = LOGSEVERITY_WARNING;
	Settings.background_color = CefColorSetARGB(255, 0, 0, 0);
	CefString(&Settings.browser_subprocess_path) = Exe;
	CefString(&Settings.resources_dir_path) = BrowserDir;
	CefString(&Settings.locales_dir_path) = BrowserDir + L"\\locales";
	CefString(&Settings.root_cache_path) = DataDir;
	CefString(&Settings.log_file) = LogDir + L"\\TelescreenBrowser.log";
	CefString(&Settings.locale) = "en-US";
	CreateDirectoryW(DataDir.c_str(), nullptr);

	const double Start = NowSeconds();
	CefMainArgs Args(GetModuleHandleW(nullptr));
	CefRefPtr<CefApp> App = new FHostApp();
	if (!CefInitialize(Args, Settings, App, nullptr))
	{
		Error = "the browser engine could not start (exit code " + std::to_string(CefGetExitCode()) + ")";
		return false;
	}
	GInitialized = true;
	LOG_INFO("browser engine started in %.2f s (CEF %s)", NowSeconds() - Start, CEF_VERSION);
	return true;
}

void Browser::Shutdown()
{
	if (!GInitialized) return;
	CefShutdown();
	GInitialized = false;
	LOG_INFO("browser engine stopped");
}

void Browser::AllowFileUrls(bool bAllow)
{
	GAllowFileUrls = bAllow;
}

FBrowserSession::FBrowserSession(const FBrowserParams& InParams, FSessionEvents InEvents)
	: Params(InParams), Events(std::move(InEvents)), Context(std::make_shared<CefRefPtr<CefRequestContext>>())
{
	Width = int(std::clamp<uint32_t>(Params.Width, 16, 3840));
	Height = int(std::clamp<uint32_t>(Params.Height, 16, 2160));
	FrameTaps::SetRefresh(Params.SessionId, [this]
	{
		std::lock_guard<std::mutex> Lock(TabsMutex);
		if (FTab* Active = FindTabLocked(ActiveTab)) Publish(*Active);
	});

	if (Params.KeepLogins && !Params.ProfileName.empty())
	{
		std::string Safe;
		for (char C : Params.ProfileName) Safe.push_back(isalnum((unsigned char)C) ? C : '_');
		CachePath = GDataDir + L"\\profiles\\" + Widen(Safe);
	}
	const std::string StartUrl = IsAllowedUrl(Params.Url) ? Params.Url : "about:blank";
	LOG_INFO("session %u: browser %ux%u at %s (%s)", Params.SessionId, Width.load(), Height.load(), StartUrl.c_str(),
		CachePath.empty() ? "clean profile" : "kept logins");
	if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Buffering, 0.0, 0.0, StartUrl);

	std::lock_guard<std::mutex> Lock(TabsMutex);
	AddTabLocked(StartUrl, true);
}

FBrowserSession::~FBrowserSession()
{
	FrameTaps::ClearRefresh(Params.SessionId);
	std::vector<CefRefPtr<FBrowserClient>> Clients;
	{
		std::lock_guard<std::mutex> Lock(TabsMutex);
		for (const auto& Tab : Tabs) Clients.push_back(Tab->Client);
		Tabs.clear();
	}
	// A detached client closes a browser that is created after this itself.
	for (const auto& Client : Clients) Client->Detach();
	std::vector<HANDLE> Waits;
	for (const auto& Client : Clients)
	{
		if (CefRefPtr<CefBrowser> BrowserRef = Client->GetBrowser())
		{
			RunOnUi([BrowserRef] { BrowserRef->GetHost()->CloseBrowser(true); });
			Waits.push_back(Client->GetClosedEvent());
		}
	}
	if (!Waits.empty() && WaitForMultipleObjects(DWORD(Waits.size()), Waits.data(), TRUE, 5000) == WAIT_TIMEOUT)
	{
		LOG_WARN("session %u: browser tabs did not close in time", Params.SessionId);
	}
	LOG_INFO("session %u: browser closed (%zu tab(s))", Params.SessionId, Clients.size());
}

uint32_t FBrowserSession::AddTabLocked(const std::string& Url, bool bActivate)
{
	auto Tab = std::make_unique<FTab>();
	Tab->Id = NextTabId++;
	Tab->Url = Url;
	Tab->Client = new FBrowserClient(this, Tab->Id);
	const uint32_t Id = Tab->Id;
	CefRefPtr<FBrowserClient> ClientRef = Tab->Client;
	Tabs.push_back(std::move(Tab));
	LOG_INFO("session %u: tab %u opens %s%s", Params.SessionId, Id, Url.substr(0, 200).c_str(), bActivate ? "" : " in the background");

	// One request context per session (made by its first tab): its tabs share cookies and logins.
	std::shared_ptr<CefRefPtr<CefRequestContext>> ContextRef = Context;
	const std::wstring Cache = CachePath;
	RunOnUi([ClientRef, Url, Cache, ContextRef]
	{
		if (!*ContextRef)
		{
			CefRequestContextSettings ContextSettings;
			if (!Cache.empty()) CefString(&ContextSettings.cache_path) = Cache;
			*ContextRef = CefRequestContext::CreateContext(ContextSettings, nullptr);
		}
		CefWindowInfo WindowInfo;
		WindowInfo.SetAsWindowless(nullptr);
		CefBrowserSettings BrowserSettings;
		BrowserSettings.windowless_frame_rate = 60;
		BrowserSettings.background_color = CefColorSetARGB(255, 0, 0, 0);
		CefBrowserHost::CreateBrowser(WindowInfo, ClientRef, Url, BrowserSettings, nullptr, *ContextRef);
	});
	if (bActivate || Tabs.size() == 1)
	{
		ActivateLocked(Id);
	}
	else
	{
		SendTabsLocked();
	}
	return Id;
}

FBrowserSession::FTab* FBrowserSession::FindTabLocked(uint32_t Tab)
{
	for (const auto& Each : Tabs)
	{
		if (Each->Id == Tab) return Each.get();
	}
	return nullptr;
}

void FBrowserSession::ActivateLocked(uint32_t Tab)
{
	FTab* Shown = FindTabLocked(Tab);
	if (!Shown) return;
	ActiveTab = Tab;
	for (const auto& Each : Tabs)
	{
		if (!Each->Browser) continue;
		CefRefPtr<CefBrowserHost> Host = Each->Browser->GetHost();
		const bool bShown = Each->Id == Tab;
		RunOnUi([Host, bShown]
		{
			Host->WasHidden(!bShown);
			Host->SetAudioMuted(!bShown);
			if (bShown)
			{
				Host->WasResized();
				Host->Invalidate(PET_VIEW);
				Host->SetFocus(true);
			}
		});
	}
	Publish(*Shown);
	SendStateLocked(*Shown);
	SendTabsLocked();
}

void FBrowserSession::OnTabCreated(uint32_t Tab, CefRefPtr<CefBrowser> BrowserRef)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Created = FindTabLocked(Tab);
	if (!Created)
	{
		// Closed before it was created.
		BrowserRef->GetHost()->CloseBrowser(true);
		return;
	}
	Created->Browser = BrowserRef;
	if (Tab != ActiveTab)
	{
		BrowserRef->GetHost()->WasHidden(true);
		BrowserRef->GetHost()->SetAudioMuted(true);
	}
}

CefRefPtr<CefBrowser> FBrowserSession::GetActiveBrowser()
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Active = FindTabLocked(ActiveTab);
	return Active ? Active->Browser : nullptr;
}

void FBrowserSession::GetViewSize(int& OutWidth, int& OutHeight) const
{
	OutWidth = Width.load();
	OutHeight = Height.load();
}

void FBrowserSession::OnPaint(uint32_t Tab, bool bPopup, const void* Buffer, int InWidth, int InHeight)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Painted = FindTabLocked(Tab);
	if (!Painted) return;
	const size_t Bytes = size_t(InWidth) * InHeight * 4;
	if (bPopup)
	{
		Painted->Popup.assign(static_cast<const uint8_t*>(Buffer), static_cast<const uint8_t*>(Buffer) + Bytes);
		Painted->PopupWidth = InWidth;
		Painted->PopupHeight = InHeight;
	}
	else
	{
		Painted->View.assign(static_cast<const uint8_t*>(Buffer), static_cast<const uint8_t*>(Buffer) + Bytes);
		Painted->ViewWidth = InWidth;
		Painted->ViewHeight = InHeight;
	}
	if (Tab == ActiveTab) Publish(*Painted);
}

void FBrowserSession::OnPopupShow(uint32_t Tab, bool bShow)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Shown = FindTabLocked(Tab);
	if (!Shown) return;
	Shown->PopupVisible = bShow;
	if (!bShow)
	{
		Shown->Popup.clear();
		if (Tab == ActiveTab) Publish(*Shown);
	}
}

void FBrowserSession::OnPopupRect(uint32_t Tab, int X, int Y, int InWidth, int InHeight)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Shown = FindTabLocked(Tab);
	if (!Shown) return;
	Shown->PopupX = X;
	Shown->PopupY = Y;
	Shown->PopupWidth = InWidth;
	Shown->PopupHeight = InHeight;
}

void FBrowserSession::Publish(FTab& Tab)
{
	if (Tab.View.empty()) return;
	if (!VideoWriter || VideoWriter->GetWidth() != uint32_t(Tab.ViewWidth) || VideoWriter->GetHeight() != uint32_t(Tab.ViewHeight))
	{
		auto Writer = std::make_unique<FVideoWriter>();
		const uint32_t Generation = ++VideoGeneration;
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-video" +
			std::to_string(Generation);
		if (!Writer->Create(Name, Tab.ViewWidth, Tab.ViewHeight)) return;
		VideoWriter = std::move(Writer);
		LOG_INFO("session %u: browser video %dx%d (generation %u)", Params.SessionId, Tab.ViewWidth, Tab.ViewHeight, Generation);
		if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, Tab.ViewWidth, Tab.ViewHeight, Name);
		if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Playing, 0.0, 0.0, std::string());
	}
	const uint8_t* Frame = Tab.View.data();
	if (Tab.PopupVisible && !Tab.Popup.empty() && size_t(Tab.PopupWidth) * Tab.PopupHeight * 4 == Tab.Popup.size())
	{
		Composed = Tab.View;
		for (int Row = 0; Row < Tab.PopupHeight; ++Row)
		{
			const int Y = Tab.PopupY + Row;
			if (Y < 0 || Y >= Tab.ViewHeight) continue;
			const int X0 = std::max(0, Tab.PopupX), X1 = std::min(Tab.ViewWidth, Tab.PopupX + Tab.PopupWidth);
			if (X1 <= X0) continue;
			memcpy(&Composed[(size_t(Y) * Tab.ViewWidth + X0) * 4], &Tab.Popup[(size_t(Row) * Tab.PopupWidth + (X0 - Tab.PopupX)) * 4],
				size_t(X1 - X0) * 4);
		}
		Frame = Composed.data();
	}
	VideoWriter->Publish(Frame, NowSeconds());
	++FramesPublished;
	FrameTaps::Publish(Params.SessionId, Frame, Tab.ViewWidth, Tab.ViewHeight);
}

void FBrowserSession::OnAudioStarted(uint32_t Tab, int Channels)
{
	std::lock_guard<std::mutex> Lock(AudioMutex);
	AudioChannels = std::max(1, Channels);
	if (AudioWriter) return;
	AudioWriter = std::make_unique<FAudioWriter>();
	const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-audio";
	if (!AudioWriter->Create(Name, AudioRate, 2, AudioCapacity))
	{
		AudioWriter.reset();
		return;
	}
	LOG_INFO("session %u: browser audio started (%d channels, tab %u)", Params.SessionId, Channels, Tab);
	if (Events.OnAudio) Events.OnAudio(Params.SessionId, AudioRate, 2, AudioCapacity, Name);
}

void FBrowserSession::OnAudioPacket(uint32_t Tab, const float** Data, int Frames)
{
	{
		// Background tabs are muted; a packet already under way when its tab was hidden is dropped here.
		std::lock_guard<std::mutex> Lock(TabsMutex);
		if (Tab != ActiveTab) return;
	}
	std::lock_guard<std::mutex> Lock(AudioMutex);
	if (Frames <= 0) return;
	Interleaved.resize(size_t(Frames) * 2);
	const float* Left = Data[0];
	const float* Right = AudioChannels > 1 ? Data[1] : Data[0];
	for (int F = 0; F < Frames; ++F)
	{
		Interleaved[size_t(F) * 2] = Left[F];
		Interleaved[size_t(F) * 2 + 1] = Right[F];
	}
	AudioTaps::Publish(Params.SessionId, Interleaved.data(), Frames);
	if (AudioWriter && AudioWriter->QueuedFrames() <= MaxAudioQueued) AudioWriter->Write(Interleaved.data(), uint32_t(Frames));
}

void FBrowserSession::OnBrowserState(uint32_t Tab, const std::string& InUrl, const std::string& InTitle, int InLoading, int InCanGoBack,
	int InCanGoForward, int InCursor, bool bPointerLocked)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	FTab* Changed = FindTabLocked(Tab);
	if (!Changed) return;
	const bool bListChanged = Changed->Url != InUrl || Changed->Title != InTitle || Changed->Loading != InLoading;
	Changed->Url = InUrl;
	Changed->Title = InTitle;
	Changed->Loading = InLoading;
	Changed->CanGoBack = InCanGoBack;
	Changed->CanGoForward = InCanGoForward;
	Changed->Cursor = InCursor;
	Changed->PointerLocked = bPointerLocked;
	if (Tab == ActiveTab) SendStateLocked(*Changed);
	if (bListChanged) SendTabsLocked();
}

void FBrowserSession::SendStateLocked(const FTab& Tab)
{
	if (Events.OnBrowserState)
	{
		Events.OnBrowserState(Params.SessionId, Tab.Url, Tab.Title, Tab.Loading != 0, Tab.CanGoBack != 0, Tab.CanGoForward != 0, uint8_t(Tab.Cursor),
			Tab.PointerLocked);
	}
}

void FBrowserSession::SendTabsLocked()
{
	if (!Events.OnBrowserTabs) return;
	std::vector<FBrowserTabInfo> List;
	List.reserve(Tabs.size());
	for (const auto& Each : Tabs)
	{
		FBrowserTabInfo Info;
		Info.Id = Each->Id;
		Info.Title = Each->Title;
		Info.Url = Each->Url;
		Info.Loading = Each->Loading != 0;
		List.push_back(std::move(Info));
	}
	Events.OnBrowserTabs(Params.SessionId, ActiveTab, List);
}

bool FBrowserSession::OnPageOpensTab(const std::string& Url, bool bForeground)
{
	std::lock_guard<std::mutex> Lock(TabsMutex);
	if (Tabs.size() >= MaxTabs)
	{
		LOG_INFO("session %u: %zu tabs open; the pop-up loads in the current tab", Params.SessionId, Tabs.size());
		return false;
	}
	AddTabLocked(Url, bForeground);
	return true;
}

void FBrowserSession::HandleInput(EInput Kind, FPayloadReader& R)
{
	CefRefPtr<CefBrowser> BrowserRef = GetActiveBrowser();
	if (!BrowserRef) return;
	CefRefPtr<CefBrowserHost> Host = BrowserRef->GetHost();
	switch (Kind)
	{
	case EInput::MouseMove:
	{
		CefMouseEvent Event;
		Event.x = int32_t(R.U32());
		Event.y = int32_t(R.U32());
		Event.modifiers = R.U32();
		if (R.Ok) RunOnUi([Host, Event] { Host->SendMouseMoveEvent(Event, false); });
		break;
	}
	case EInput::MouseButton:
	{
		CefMouseEvent Event;
		Event.x = int32_t(R.U32());
		Event.y = int32_t(R.U32());
		const uint8_t Button = R.U8();
		const bool bUp = R.U8() != 0;
		const int Clicks = std::clamp<int>(R.U8(), 1, 3);
		Event.modifiers = R.U32();
		const cef_mouse_button_type_t Type = Button == 1 ? MBT_MIDDLE : Button == 2 ? MBT_RIGHT : MBT_LEFT;
		if (R.Ok) RunOnUi([Host, Event, Type, bUp, Clicks] { Host->SendMouseClickEvent(Event, Type, bUp, Clicks); });
		break;
	}
	case EInput::Wheel:
	{
		CefMouseEvent Event;
		Event.x = int32_t(R.U32());
		Event.y = int32_t(R.U32());
		const int DeltaX = int32_t(R.U32());
		const int DeltaY = int32_t(R.U32());
		Event.modifiers = R.U32();
		if (R.Ok) RunOnUi([Host, Event, DeltaX, DeltaY] { Host->SendMouseWheelEvent(Event, DeltaX, DeltaY); });
		break;
	}
	case EInput::Key:
	{
		CefKeyEvent Event;
		Event.type = cef_key_event_type_t(R.U8());
		Event.windows_key_code = int32_t(R.U32());
		Event.native_key_code = int32_t(R.U32());
		uint16_t Character = 0;
		R.Read(&Character, 2);
		Event.character = Event.unmodified_character = Character;
		Event.modifiers = R.U32();
		Event.is_system_key = (Event.modifiers & TSHostProtocol::ModAlt) != 0;
		if (R.Ok) RunOnUi([Host, Event] { Host->SendKeyEvent(Event); });
		break;
	}
	case EInput::Text:
	{
		const std::string Text = R.Str();
		if (R.Ok && !Text.empty())
		{
			RunOnUi([Host, Text] { Host->ImeCommitText(Text, CefRange::InvalidRange(), 0); });
		}
		break;
	}
	case EInput::MouseLeave:
		RunOnUi([Host] { Host->SendMouseMoveEvent(CefMouseEvent(), true); });
		break;
	case EInput::Focus:
	{
		const bool bFocus = R.U8() != 0;
		RunOnUi([Host, bFocus] { Host->SetFocus(bFocus); });
		break;
	}
	case EInput::PointerDelta:
	{
		const int DeltaX = int32_t(R.U32());
		const int DeltaY = int32_t(R.U32());
		if (!R.Ok) break;
		const std::string Code = "window.__tsPointerMove&&__tsPointerMove(" + std::to_string(DeltaX) + "," + std::to_string(DeltaY) + ")";
		RunOnUi([BrowserRef, Code]
		{
			std::vector<CefString> Ids;
			BrowserRef->GetFrameIdentifiers(Ids);
			for (const CefString& Id : Ids)
			{
				if (CefRefPtr<CefFrame> Frame = BrowserRef->GetFrameByIdentifier(Id)) Frame->ExecuteJavaScript(Code, "", 0);
			}
		});
		break;
	}
	}
}

void FBrowserSession::HandleCommand(ECommand Command, FPayloadReader& R)
{
	auto ToAddress = [](std::string Target)
	{
		if (Target.find("://") == std::string::npos && Target.rfind("about:", 0) != 0) Target = "https://" + Target;
		return Target;
	};
	switch (Command)
	{
	case ECommand::NewTab:
	{
		const std::string Target = ToAddress(R.Str());
		if (!R.Ok) return;
		if (!IsAllowedUrl(Target))
		{
			LOG_WARN("session %u: refused to open %s", Params.SessionId, Target.substr(0, 200).c_str());
			return;
		}
		std::lock_guard<std::mutex> Lock(TabsMutex);
		if (Tabs.size() >= MaxTabs)
		{
			LOG_INFO("session %u: no new tab, %zu are open", Params.SessionId, Tabs.size());
			return;
		}
		AddTabLocked(Target, true);
		return;
	}
	case ECommand::SelectTab:
	{
		const uint32_t Tab = R.U32();
		if (!R.Ok) return;
		std::lock_guard<std::mutex> Lock(TabsMutex);
		if (Tab != ActiveTab && FindTabLocked(Tab))
		{
			LOG_INFO("session %u: shows tab %u", Params.SessionId, Tab);
			ActivateLocked(Tab);
		}
		return;
	}
	case ECommand::CloseTab:
	{
		const uint32_t Tab = R.U32();
		if (!R.Ok) return;
		CefRefPtr<FBrowserClient> Closed;
		{
			std::lock_guard<std::mutex> Lock(TabsMutex);
			// The last tab stays: the game closes the whole session instead.
			if (Tabs.size() <= 1) return;
			auto It = std::find_if(Tabs.begin(), Tabs.end(), [Tab](const std::unique_ptr<FTab>& Each) { return Each->Id == Tab; });
			if (It == Tabs.end()) return;
			const size_t Index = size_t(It - Tabs.begin());
			Closed = (*It)->Client;
			Tabs.erase(It);
			LOG_INFO("session %u: closed tab %u, %zu left", Params.SessionId, Tab, Tabs.size());
			if (Tab == ActiveTab)
			{
				ActivateLocked(Tabs[std::min(Index, Tabs.size() - 1)]->Id);
			}
			else
			{
				SendTabsLocked();
			}
		}
		Closed->Detach();
		if (CefRefPtr<CefBrowser> BrowserRef = Closed->GetBrowser())
		{
			RunOnUi([BrowserRef] { BrowserRef->GetHost()->CloseBrowser(true); });
		}
		return;
	}
	case ECommand::Resize:
	{
		const uint32_t W = R.U32(), H = R.U32();
		if (!R.Ok) return;
		Width = int(std::clamp<uint32_t>(W, 16, 3840));
		Height = int(std::clamp<uint32_t>(H, 16, 2160));
		std::lock_guard<std::mutex> Lock(TabsMutex);
		for (const auto& Each : Tabs)
		{
			if (CefRefPtr<CefBrowser> BrowserRef = Each->Browser) RunOnUi([BrowserRef] { BrowserRef->GetHost()->WasResized(); });
		}
		return;
	}
	default:
		break;
	}
	CefRefPtr<CefBrowser> BrowserRef = GetActiveBrowser();
	if (!BrowserRef) return;
	switch (Command)
	{
	case ECommand::Navigate:
	{
		const std::string Target = ToAddress(R.Str());
		if (!R.Ok) return;
		if (!IsAllowedUrl(Target))
		{
			LOG_WARN("session %u: refused to open %s", Params.SessionId, Target.substr(0, 200).c_str());
			return;
		}
		RunOnUi([BrowserRef, Target] { BrowserRef->GetMainFrame()->LoadURL(Target); });
		break;
	}
	case ECommand::Back: RunOnUi([BrowserRef] { BrowserRef->GoBack(); }); break;
	case ECommand::Forward: RunOnUi([BrowserRef] { BrowserRef->GoForward(); }); break;
	case ECommand::Reload: RunOnUi([BrowserRef] { BrowserRef->Reload(); }); break;
	case ECommand::Stop: RunOnUi([BrowserRef] { BrowserRef->StopLoad(); }); break;
	default: break;
	}
}
