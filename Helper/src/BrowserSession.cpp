#include "BrowserSession.h"

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

// CEF handlers for one browser; forwards everything to its session until the session detaches.
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
	explicit FBrowserClient(FBrowserSession* InSession) : Session(InSession) {}

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
		WithSession([&](FBrowserSession& S) { S.OnPaint(Type == PET_POPUP, Buffer, W, H); });
	}
	void OnPopupShow(CefRefPtr<CefBrowser>, bool bShow) override
	{
		WithSession([&](FBrowserSession& S) { S.OnPopupShow(bShow); });
	}
	void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& Rect) override
	{
		WithSession([&](FBrowserSession& S) { S.OnPopupRect(Rect.x, Rect.y, Rect.width, Rect.height); });
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
		WithSession([&](FBrowserSession& S) { S.OnAudioStarted(Channels); });
	}
	void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** Data, int Frames, int64_t) override
	{
		WithSession([&](FBrowserSession& S) { S.OnAudioPacket(Data, Frames); });
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

	// Life span: pop-ups open in the same view; nothing creates a new window.
	void OnAfterCreated(CefRefPtr<CefBrowser> InBrowser) override
	{
		{
			std::lock_guard<std::mutex> Lock(BrowserMutex);
			Browser = InBrowser;
		}
		CefRefPtr<CefDictionaryValue> Script = CefDictionaryValue::Create();
		Script->SetString("source", PointerLockScript);
		InBrowser->GetHost()->ExecuteDevToolsMethod(0, "Page.addScriptToEvaluateOnNewDocument", Script);
	}
	bool OnBeforePopup(CefRefPtr<CefBrowser> InBrowser, CefRefPtr<CefFrame>, int, const CefString& TargetUrl, const CefString&,
		cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
		CefRefPtr<CefDictionaryValue>&, bool*) override
	{
		if (IsAllowedUrl(TargetUrl.ToString())) InBrowser->GetMainFrame()->LoadURL(TargetUrl);
		return true;
	}
	void OnBeforeClose(CefRefPtr<CefBrowser>) override
	{
		{
			std::lock_guard<std::mutex> Lock(BrowserMutex);
			Browser = nullptr;
		}
		WithSession([](FBrowserSession& S) { S.OnClosed(); });
	}

	// Requests: only allowed schemes, new tabs stay in this view.
	bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> Request, bool, bool) override
	{
		const std::string Target = Request->GetURL().ToString();
		if (IsAllowedUrl(Target)) return false;
		LOG_WARN("browser: blocked navigation to %s", Target.substr(0, 200).c_str());
		return true;
	}
	bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame, const CefString& TargetUrl, cef_window_open_disposition_t, bool) override
	{
		if (IsAllowedUrl(TargetUrl.ToString())) Frame->LoadURL(TargetUrl);
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
		WithSession([&](FBrowserSession& S) { S.OnBrowserState(Url, Title, Loading, CanGoBack, CanGoForward, Cursor, PointerLocked); });
	}

	std::mutex SessionMutex;
	FBrowserSession* Session;
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
	: Params(InParams), Events(std::move(InEvents))
{
	Width = int(std::clamp<uint32_t>(Params.Width, 16, 3840));
	Height = int(std::clamp<uint32_t>(Params.Height, 16, 2160));
	ClosedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	Client = new FBrowserClient(this);
	Client->AddRef();

	std::wstring CachePath;
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

	FBrowserClient* RawClient = Client;
	RunOnUi([RawClient, StartUrl, CachePath]
	{
		CefRequestContextSettings ContextSettings;
		if (!CachePath.empty())
		{
			CefString(&ContextSettings.cache_path) = CachePath;
		}
		CefRefPtr<CefRequestContext> Context = CefRequestContext::CreateContext(ContextSettings, nullptr);
		CefWindowInfo WindowInfo;
		WindowInfo.SetAsWindowless(nullptr);
		CefBrowserSettings BrowserSettings;
		BrowserSettings.windowless_frame_rate = 60;
		BrowserSettings.background_color = CefColorSetARGB(255, 0, 0, 0);
		CefBrowserHost::CreateBrowser(WindowInfo, RawClient, StartUrl, BrowserSettings, nullptr, Context);
	});
}

FBrowserSession::~FBrowserSession()
{
	CefRefPtr<CefBrowser> BrowserRef = Client->GetBrowser();
	if (BrowserRef)
	{
		RunOnUi([BrowserRef] { BrowserRef->GetHost()->CloseBrowser(true); });
		if (WaitForSingleObject(ClosedEvent, 5000) != WAIT_OBJECT_0) LOG_WARN("session %u: browser did not close in time", Params.SessionId);
	}
	Client->Detach();
	Client->Release();
	CloseHandle(ClosedEvent);
	LOG_INFO("session %u: browser closed", Params.SessionId);
}

void FBrowserSession::GetViewSize(int& OutWidth, int& OutHeight) const
{
	OutWidth = Width.load();
	OutHeight = Height.load();
}

void FBrowserSession::OnClosed()
{
	SetEvent(ClosedEvent);
}

void FBrowserSession::OnPaint(bool bPopup, const void* Buffer, int InWidth, int InHeight)
{
	std::lock_guard<std::mutex> Lock(PaintMutex);
	const size_t Bytes = size_t(InWidth) * InHeight * 4;
	if (bPopup)
	{
		Popup.assign(static_cast<const uint8_t*>(Buffer), static_cast<const uint8_t*>(Buffer) + Bytes);
		PopupWidth = InWidth;
		PopupHeight = InHeight;
	}
	else
	{
		View.assign(static_cast<const uint8_t*>(Buffer), static_cast<const uint8_t*>(Buffer) + Bytes);
		ViewWidth = InWidth;
		ViewHeight = InHeight;
	}
	Publish();
}

void FBrowserSession::OnPopupShow(bool bShow)
{
	std::lock_guard<std::mutex> Lock(PaintMutex);
	PopupVisible = bShow;
	if (!bShow)
	{
		Popup.clear();
		Publish();
	}
}

void FBrowserSession::OnPopupRect(int X, int Y, int InWidth, int InHeight)
{
	std::lock_guard<std::mutex> Lock(PaintMutex);
	PopupX = X;
	PopupY = Y;
	PopupWidth = InWidth;
	PopupHeight = InHeight;
}

void FBrowserSession::Publish()
{
	if (View.empty()) return;
	if (!VideoWriter || VideoWriter->GetWidth() != uint32_t(ViewWidth) || VideoWriter->GetHeight() != uint32_t(ViewHeight))
	{
		auto Writer = std::make_unique<FVideoWriter>();
		const uint32_t Generation = ++VideoGeneration;
		const std::string Name = "Local\\Telescreen-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(Params.SessionId) + "-video" +
			std::to_string(Generation);
		if (!Writer->Create(Name, ViewWidth, ViewHeight)) return;
		VideoWriter = std::move(Writer);
		LOG_INFO("session %u: browser video %dx%d (generation %u)", Params.SessionId, ViewWidth, ViewHeight, Generation);
		if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, ViewWidth, ViewHeight, Name);
		if (Events.OnState) Events.OnState(Params.SessionId, ESessionState::Playing, 0.0, 0.0, std::string());
	}
	const uint8_t* Frame = View.data();
	if (PopupVisible && !Popup.empty() && size_t(PopupWidth) * PopupHeight * 4 == Popup.size())
	{
		Composed = View;
		for (int Row = 0; Row < PopupHeight; ++Row)
		{
			const int Y = PopupY + Row;
			if (Y < 0 || Y >= ViewHeight) continue;
			const int X0 = std::max(0, PopupX), X1 = std::min(ViewWidth, PopupX + PopupWidth);
			if (X1 <= X0) continue;
			memcpy(&Composed[(size_t(Y) * ViewWidth + X0) * 4], &Popup[(size_t(Row) * PopupWidth + (X0 - PopupX)) * 4], size_t(X1 - X0) * 4);
		}
		Frame = Composed.data();
	}
	VideoWriter->Publish(Frame, NowSeconds());
	++FramesPublished;
}

void FBrowserSession::OnAudioStarted(int Channels)
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
	LOG_INFO("session %u: browser audio started (%d channels)", Params.SessionId, Channels);
	if (Events.OnAudio) Events.OnAudio(Params.SessionId, AudioRate, 2, AudioCapacity, Name);
}

void FBrowserSession::OnAudioPacket(const float** Data, int Frames)
{
	std::lock_guard<std::mutex> Lock(AudioMutex);
	if (!AudioWriter || Frames <= 0 || AudioWriter->QueuedFrames() > MaxAudioQueued) return;
	Interleaved.resize(size_t(Frames) * 2);
	const float* Left = Data[0];
	const float* Right = AudioChannels > 1 ? Data[1] : Data[0];
	for (int F = 0; F < Frames; ++F)
	{
		Interleaved[size_t(F) * 2] = Left[F];
		Interleaved[size_t(F) * 2 + 1] = Right[F];
	}
	AudioWriter->Write(Interleaved.data(), uint32_t(Frames));
}

void FBrowserSession::OnBrowserState(const std::string& InUrl, const std::string& InTitle, int InLoading, int InCanGoBack, int InCanGoForward,
	int InCursor, bool bPointerLocked)
{
	if (Events.OnBrowserState)
	{
		Events.OnBrowserState(Params.SessionId, InUrl, InTitle, InLoading != 0, InCanGoBack != 0, InCanGoForward != 0, uint8_t(InCursor),
			bPointerLocked);
	}
}

void FBrowserSession::HandleInput(EInput Kind, FPayloadReader& R)
{
	CefRefPtr<CefBrowser> BrowserRef = Client->GetBrowser();
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
	CefRefPtr<CefBrowser> BrowserRef = Client->GetBrowser();
	if (!BrowserRef) return;
	switch (Command)
	{
	case ECommand::Navigate:
	{
		std::string Target = R.Str();
		if (!R.Ok) return;
		if (Target.find("://") == std::string::npos && Target.rfind("about:", 0) != 0) Target = "https://" + Target;
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
	case ECommand::Resize:
	{
		const uint32_t W = R.U32(), H = R.U32();
		if (!R.Ok) return;
		Width = int(std::clamp<uint32_t>(W, 16, 3840));
		Height = int(std::clamp<uint32_t>(H, 16, 2160));
		RunOnUi([BrowserRef] { BrowserRef->GetHost()->WasResized(); });
		break;
	}
	}
}
