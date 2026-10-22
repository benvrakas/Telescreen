#include "WindowSession.h"
#include "Taps.h"

#include <Unknwn.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <mmdeviceapi.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>

extern "C"
{
#include <libswscale/swscale.h>
}

using TSHostProtocol::EInput;
using TSHostProtocol::ESessionState;

namespace WGC = winrt::Windows::Graphics::Capture;
namespace WD3D = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace
{
	constexpr int AudioRate = 48000;
	constexpr double MinPublishInterval = 1.0 / 60.0;

	std::string ExeName(uint32_t ProcessId)
	{
		HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId);
		if (!Process) return std::string();
		wchar_t Path[MAX_PATH] = {};
		DWORD Size = MAX_PATH;
		std::string Name;
		if (QueryFullProcessImageNameW(Process, 0, Path, &Size))
		{
			const std::wstring Full(Path, Size);
			const size_t Slash = Full.find_last_of(L"\\/");
			Name = Narrow(Slash == std::wstring::npos ? Full : Full.substr(Slash + 1));
		}
		CloseHandle(Process);
		return Name;
	}

	bool IsCloaked(HWND Window)
	{
		DWORD Cloaked = 0;
		return SUCCEEDED(DwmGetWindowAttribute(Window, DWMWA_CLOAKED, &Cloaked, sizeof(Cloaked))) && Cloaked != 0;
	}

	// Completion handler for ActivateAudioInterfaceAsync (agile, as the API requires).
	struct FActivateHandler : winrt::implements<FActivateHandler, IActivateAudioInterfaceCompletionHandler>
	{
		HANDLE Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HRESULT Result = E_FAIL;
		winrt::com_ptr<IAudioClient> Client;

		~FActivateHandler() { CloseHandle(Done); }

		HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* Operation) noexcept override
		{
			winrt::com_ptr<::IUnknown> Unknown;
			HRESULT Activated = E_FAIL;
			if (SUCCEEDED(Operation->GetActivateResult(&Activated, Unknown.put())) && SUCCEEDED(Activated) && Unknown)
			{
				Client = Unknown.try_as<IAudioClient>();
			}
			Result = Client ? S_OK : (FAILED(Activated) ? Activated : E_NOINTERFACE);
			SetEvent(Done);
			return S_OK;
		}
	};
}

std::vector<FWindowInfo> ListShareableWindows(uint32_t ExcludeProcess)
{
	struct FContext
	{
		std::vector<FWindowInfo> Windows;
		uint32_t Exclude;
	} Context{{}, ExcludeProcess};
	EnumWindows([](HWND Window, LPARAM Param) -> BOOL
	{
		FContext& C = *reinterpret_cast<FContext*>(Param);
		if (!IsWindowVisible(Window) || GetWindow(Window, GW_OWNER) || (GetWindowLongW(Window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || IsCloaked(Window))
		{
			return TRUE;
		}
		const int Length = GetWindowTextLengthW(Window);
		if (Length <= 0) return TRUE;
		DWORD ProcessId = 0;
		GetWindowThreadProcessId(Window, &ProcessId);
		if (ProcessId == C.Exclude || ProcessId == GetCurrentProcessId()) return TRUE;
		std::wstring Title(size_t(Length) + 1, L'\0');
		Title.resize(size_t(GetWindowTextW(Window, Title.data(), Length + 1)));
		FWindowInfo Info;
		Info.Window = uint64_t(reinterpret_cast<uintptr_t>(Window));
		Info.ProcessId = ProcessId;
		Info.Title = Narrow(Title);
		Info.Exe = ExeName(ProcessId);
		C.Windows.push_back(std::move(Info));
		return C.Windows.size() < 200;
	}, reinterpret_cast<LPARAM>(&Context));
	return Context.Windows;
}

FWindowSession::FWindowSession(const FWindowParams& InParams, FSessionEvents InEvents)
	: Params(InParams), Events(std::move(InEvents))
{
	Window = reinterpret_cast<HWND>(uintptr_t(Params.Window));
	StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	DWORD Pid = 0;
	if (IsWindow(Window)) GetWindowThreadProcessId(Window, &Pid);
	ProcessId = Pid;
	FrameTaps::SetRefresh(Params.SessionId, [this]
	{
		std::lock_guard<std::mutex> Lock(FrameMutex);
		Publish();
	});
	if (!ProcessId)
	{
		LOG_WARN("session %u: window %llx no longer exists", Params.SessionId, (unsigned long long)Params.Window);
		SetState(ESessionState::Error, "That window is gone; pick it again.");
		return;
	}
	LOG_INFO("session %u: sharing window %llx (%s, pid %u), cursor %d, sound %d", Params.SessionId, (unsigned long long)Params.Window,
		ExeName(ProcessId).c_str(), ProcessId, int(Params.bCursor), int(Params.bAudio));
	SetState(ESessionState::Buffering, "Starting the capture");
	CaptureThread = std::thread([this] { RunCapture(); });
	WatchThread = std::thread([this] { RunWatch(); });
	if (Params.bAudio) AudioThread = std::thread([this] { RunAudio(); });
}

FWindowSession::~FWindowSession()
{
	FrameTaps::ClearRefresh(Params.SessionId);
	Stopping = true;
	SetEvent(StopEvent);
	if (CaptureThread.joinable()) CaptureThread.join();
	if (AudioThread.joinable()) AudioThread.join();
	if (WatchThread.joinable()) WatchThread.join();
	CloseHandle(StopEvent);
	sws_freeContext(Scaler);
	LOG_INFO("session %u: window sharing closed", Params.SessionId);
}

void FWindowSession::SetState(ESessionState State, const std::string& Message)
{
	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		if (State == LastState && Message == LastMessage) return;
		LastState = State;
		LastMessage = Message;
	}
	if (Events.OnState) Events.OnState(Params.SessionId, State, 0.0, 0.0, Message);
}

void FWindowSession::RunCapture()
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	try
	{
		winrt::com_ptr<ID3D11Device> Device;
		winrt::com_ptr<ID3D11DeviceContext> Context;
		winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
			Device.put(), nullptr, Context.put()));
		winrt::com_ptr<::IInspectable> Inspectable;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(Device.as<IDXGIDevice>().get(), Inspectable.put()));
		const WD3D::IDirect3DDevice WinrtDevice = Inspectable.as<WD3D::IDirect3DDevice>();

		auto Interop = winrt::get_activation_factory<WGC::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		WGC::GraphicsCaptureItem Item{nullptr};
		winrt::check_hresult(Interop->CreateForWindow(Window, winrt::guid_of<WGC::GraphicsCaptureItem>(), winrt::put_abi(Item)));
		winrt::Windows::Graphics::SizeInt32 PoolSize = Item.Size();
		const auto Format = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized;
		WGC::Direct3D11CaptureFramePool Pool = WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(WinrtDevice, Format, 2, PoolSize);
		WGC::GraphicsCaptureSession Capture = Pool.CreateCaptureSession(Item);
		try
		{
			Capture.IsCursorCaptureEnabled(Params.bCursor);
		}
		catch (...)
		{
		}
		try
		{
			Capture.IsBorderRequired(false);
		}
		catch (...)
		{
		}

		std::mutex CaptureMutex;
		bool bTornDown = false;
		winrt::com_ptr<ID3D11Texture2D> Staging;
		UINT StagingWidth = 0, StagingHeight = 0;
		auto Token = Pool.FrameArrived([&](const WGC::Direct3D11CaptureFramePool& Sender, const winrt::Windows::Foundation::IInspectable&)
		{
			std::lock_guard<std::mutex> Lock(CaptureMutex);
			if (bTornDown) return;
			WGC::Direct3D11CaptureFrame Frame = Sender.TryGetNextFrame();
			if (!Frame) return;
			const auto ContentSize = Frame.ContentSize();
			const double Now = NowSeconds();
			if (Now - LastPublish >= MinPublishInterval)
			{
				LastPublish = Now;
				auto Access = Frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
				winrt::com_ptr<ID3D11Texture2D> Texture;
				if (SUCCEEDED(Access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), Texture.put_void())))
				{
					D3D11_TEXTURE2D_DESC Desc{};
					Texture->GetDesc(&Desc);
					if (!Staging || StagingWidth != Desc.Width || StagingHeight != Desc.Height)
					{
						D3D11_TEXTURE2D_DESC StagingDesc = Desc;
						StagingDesc.Usage = D3D11_USAGE_STAGING;
						StagingDesc.BindFlags = 0;
						StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						StagingDesc.MiscFlags = 0;
						StagingDesc.MipLevels = 1;
						StagingDesc.ArraySize = 1;
						Staging = nullptr;
						if (SUCCEEDED(Device->CreateTexture2D(&StagingDesc, nullptr, Staging.put())))
						{
							StagingWidth = Desc.Width;
							StagingHeight = Desc.Height;
						}
					}
					if (Staging)
					{
						Context->CopyResource(Staging.get(), Texture.get());
						D3D11_MAPPED_SUBRESOURCE Mapped{};
						if (SUCCEEDED(Context->Map(Staging.get(), 0, D3D11_MAP_READ, 0, &Mapped)))
						{
							const int Width = std::min<int>(ContentSize.Width, int(Desc.Width)) & ~1;
							const int Height = std::min<int>(ContentSize.Height, int(Desc.Height)) & ~1;
							if (Width >= 2 && Height >= 2)
							{
								OnFrame(static_cast<const uint8_t*>(Mapped.pData), int(Mapped.RowPitch), Width, Height);
							}
							Context->Unmap(Staging.get(), 0);
						}
					}
				}
			}
			if (ContentSize.Width != PoolSize.Width || ContentSize.Height != PoolSize.Height)
			{
				PoolSize = ContentSize;
				Sender.Recreate(WinrtDevice, Format, 2, PoolSize);
			}
		});
		Capture.StartCapture();
		LOG_INFO("session %u: capture started at %dx%d", Params.SessionId, PoolSize.Width, PoolSize.Height);
		WaitForSingleObject(StopEvent, INFINITE);
		{
			std::lock_guard<std::mutex> Lock(CaptureMutex);
			bTornDown = true;
		}
		Pool.FrameArrived(Token);
		Capture.Close();
		Pool.Close();
	}
	catch (const winrt::hresult_error& Error)
	{
		const std::string Message = Narrow(std::wstring(Error.message().c_str()));
		LOG_ERROR("session %u: window capture failed (0x%08x): %s", Params.SessionId, uint32_t(Error.code()), Message.c_str());
		SetState(ESessionState::Error, "Could not capture that window: " + Message);
	}
	winrt::uninit_apartment();
}

void FWindowSession::OnFrame(const uint8_t* Bgra, int RowPitch, int Width, int Height)
{
	std::lock_guard<std::mutex> Lock(FrameMutex);
	const double Scale = std::min({1.0, double(Params.MaxWidth) / Width, double(Params.MaxHeight) / Height});
	const int OutWidth = std::max(2, int(Width * Scale) & ~1);
	const int OutHeight = std::max(2, int(Height * Scale) & ~1);
	View.resize(size_t(OutWidth) * OutHeight * 4);
	if (OutWidth == Width && OutHeight == Height)
	{
		for (int Row = 0; Row < Height; ++Row)
		{
			memcpy(&View[size_t(Row) * Width * 4], Bgra + size_t(Row) * RowPitch, size_t(Width) * 4);
		}
	}
	else
	{
		Scaler = sws_getCachedContext(Scaler, Width, Height, AV_PIX_FMT_BGRA, OutWidth, OutHeight, AV_PIX_FMT_BGRA, SWS_AREA, nullptr, nullptr, nullptr);
		if (!Scaler) return;
		const uint8_t* Src[1] = {Bgra};
		const int SrcStride[1] = {RowPitch};
		uint8_t* Dst[1] = {View.data()};
		const int DstStride[1] = {OutWidth * 4};
		sws_scale(Scaler, Src, SrcStride, 0, Height, Dst, DstStride);
	}
	ViewWidth = OutWidth;
	ViewHeight = OutHeight;
	Publish();
}

void FWindowSession::Publish()
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
		LOG_INFO("session %u: window video %dx%d (generation %u)", Params.SessionId, ViewWidth, ViewHeight, Generation);
		if (Events.OnVideo) Events.OnVideo(Params.SessionId, Generation, ViewWidth, ViewHeight, Name);
		SetState(ESessionState::Playing, std::string());
	}
	VideoWriter->Publish(View.data(), NowSeconds());
	FrameTaps::Publish(Params.SessionId, View.data(), ViewWidth, ViewHeight);
}

void FWindowSession::RunWatch()
{
	bool bWasMinimised = false;
	while (WaitForSingleObject(StopEvent, 500) == WAIT_TIMEOUT)
	{
		if (!IsWindow(Window))
		{
			LOG_INFO("session %u: the shared window closed", Params.SessionId);
			SetState(ESessionState::Error, "The shared window was closed.");
			return;
		}
		const bool bMinimised = IsIconic(Window) != FALSE;
		if (bMinimised != bWasMinimised)
		{
			bWasMinimised = bMinimised;
			LOG_INFO("session %u: window %s", Params.SessionId, bMinimised ? "minimised" : "restored");
			SetState(bMinimised ? ESessionState::Buffering : ESessionState::Playing,
				bMinimised ? "The window is minimised: restore it to keep sharing (it can stay behind the game)." : std::string());
		}
	}
}

void FWindowSession::RunAudio()
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	AUDIOCLIENT_ACTIVATION_PARAMS Activation{};
	Activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
	Activation.ProcessLoopbackParams.TargetProcessId = ProcessId;
	Activation.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
	PROPVARIANT Variant{};
	Variant.vt = VT_BLOB;
	Variant.blob.cbSize = sizeof(Activation);
	Variant.blob.pBlobData = reinterpret_cast<BYTE*>(&Activation);

	winrt::com_ptr<FActivateHandler> Handler = winrt::make_self<FActivateHandler>();
	winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> Operation;
	HRESULT Hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &Variant, Handler.get(), Operation.put());
	if (SUCCEEDED(Hr))
	{
		const HANDLE Waits[2] = {Handler->Done, StopEvent};
		if (WaitForMultipleObjects(2, Waits, FALSE, 5000) != WAIT_OBJECT_0)
		{
			Hr = E_ABORT;
		}
		else
		{
			Hr = Handler->Result;
		}
	}
	winrt::com_ptr<IAudioClient> Client = SUCCEEDED(Hr) ? Handler->Client : nullptr;
	WAVEFORMATEX Format{};
	Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	Format.nChannels = 2;
	Format.nSamplesPerSec = AudioRate;
	Format.wBitsPerSample = 32;
	Format.nBlockAlign = Format.nChannels * Format.wBitsPerSample / 8;
	Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
	HANDLE Ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	winrt::com_ptr<IAudioCaptureClient> CaptureClient;
	if (Client)
	{
		Hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
			200000, 0, &Format, nullptr);
		if (SUCCEEDED(Hr)) Hr = Client->SetEventHandle(Ready);
		if (SUCCEEDED(Hr)) Hr = Client->GetService(__uuidof(IAudioCaptureClient), CaptureClient.put_void());
		if (SUCCEEDED(Hr)) Hr = Client->Start();
	}
	if (FAILED(Hr) || !CaptureClient)
	{
		LOG_WARN("session %u: no window sound (process loopback 0x%08x; needs Windows 10 21H2+ or Windows 11)", Params.SessionId, uint32_t(Hr));
		CloseHandle(Ready);
		winrt::uninit_apartment();
		return;
	}
	LOG_INFO("session %u: capturing the sound of pid %u", Params.SessionId, ProcessId);
	std::vector<float> Silence;
	const HANDLE Waits[2] = {Ready, StopEvent};
	while (WaitForMultipleObjects(2, Waits, FALSE, 200) != WAIT_OBJECT_0 + 1)
	{
		UINT32 PacketFrames = 0;
		while (SUCCEEDED(CaptureClient->GetNextPacketSize(&PacketFrames)) && PacketFrames > 0)
		{
			BYTE* Data = nullptr;
			UINT32 Frames = 0;
			DWORD Flags = 0;
			if (FAILED(CaptureClient->GetBuffer(&Data, &Frames, &Flags, nullptr, nullptr))) break;
			if (Frames > 0)
			{
				const float* Samples = reinterpret_cast<const float*>(Data);
				if (Flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					Silence.assign(size_t(Frames) * 2, 0.f);
					Samples = Silence.data();
				}
				AudioTaps::Publish(Params.SessionId, Samples, int(Frames));
			}
			CaptureClient->ReleaseBuffer(Frames);
		}
	}
	Client->Stop();
	CloseHandle(Ready);
	winrt::uninit_apartment();
}

void FWindowSession::PostMouse(UINT Message, int X, int Y, WPARAM Buttons)
{
	if (!IsWindow(Window)) return;
	RECT Bounds{};
	if (FAILED(DwmGetWindowAttribute(Window, DWMWA_EXTENDED_FRAME_BOUNDS, &Bounds, sizeof(Bounds))) || ViewWidth <= 0 || ViewHeight <= 0) return;
	POINT Point{Bounds.left + int(int64_t(X) * (Bounds.right - Bounds.left) / ViewWidth), Bounds.top + int(int64_t(Y) * (Bounds.bottom - Bounds.top) / ViewHeight)};
	// Deepest child under the point, as the window would route a real click.
	HWND Target = Window;
	for (int Depth = 0; Depth < 16; ++Depth)
	{
		POINT Local = Point;
		ScreenToClient(Target, &Local);
		HWND Child = RealChildWindowFromPoint(Target, Local);
		if (!Child || Child == Target) break;
		Target = Child;
	}
	if (Message == WM_MOUSEWHEEL || Message == WM_MOUSEHWHEEL)
	{
		PostMessageW(Target, Message, Buttons, MAKELPARAM(Point.x, Point.y));
		return;
	}
	POINT Client = Point;
	ScreenToClient(Target, &Client);
	PostMessageW(Target, Message, Buttons, MAKELPARAM(Client.x, Client.y));
}

void FWindowSession::HandleInput(EInput Kind, FPayloadReader& R)
{
	// The capture is in physical pixels; work in them too.
	const DPI_AWARENESS_CONTEXT Previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	auto ButtonBits = [](uint32_t Modifiers)
	{
		WPARAM Bits = 0;
		if (Modifiers & TSHostProtocol::ModLeftButton) Bits |= MK_LBUTTON;
		if (Modifiers & TSHostProtocol::ModMiddleButton) Bits |= MK_MBUTTON;
		if (Modifiers & TSHostProtocol::ModRightButton) Bits |= MK_RBUTTON;
		if (Modifiers & TSHostProtocol::ModShift) Bits |= MK_SHIFT;
		if (Modifiers & TSHostProtocol::ModControl) Bits |= MK_CONTROL;
		return Bits;
	};
	switch (Kind)
	{
	case EInput::MouseMove:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		const uint32_t Modifiers = R.U32();
		if (R.Ok) PostMouse(WM_MOUSEMOVE, X, Y, ButtonBits(Modifiers));
		break;
	}
	case EInput::MouseButton:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		const uint8_t Button = R.U8();
		const bool bUp = R.U8() != 0;
		const int Clicks = R.U8();
		const uint32_t Modifiers = R.U32();
		if (!R.Ok) break;
		UINT Message = 0;
		if (Button == 1) Message = bUp ? WM_MBUTTONUP : (Clicks == 2 ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN);
		else if (Button == 2) Message = bUp ? WM_RBUTTONUP : (Clicks == 2 ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN);
		else Message = bUp ? WM_LBUTTONUP : (Clicks == 2 ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN);
		PostMouse(Message, X, Y, ButtonBits(Modifiers));
		break;
	}
	case EInput::Wheel:
	{
		const int X = int32_t(R.U32()), Y = int32_t(R.U32());
		const int DeltaX = int32_t(R.U32()), DeltaY = int32_t(R.U32());
		const uint32_t Modifiers = R.U32();
		if (!R.Ok) break;
		if (DeltaY != 0) PostMouse(WM_MOUSEWHEEL, X, Y, MAKEWPARAM(ButtonBits(Modifiers), short(DeltaY)));
		if (DeltaX != 0) PostMouse(WM_MOUSEHWHEEL, X, Y, MAKEWPARAM(ButtonBits(Modifiers), short(DeltaX)));
		break;
	}
	case EInput::Key:
	{
		const uint8_t Type = R.U8();
		const int32_t KeyCode = int32_t(R.U32());
		const int32_t Native = int32_t(R.U32());
		uint16_t Character = 0;
		R.Read(&Character, 2);
		R.U32();
		if (!R.Ok) break;
		// Keys go to the window's focused control, as real typing would.
		GUITHREADINFO Info{sizeof(Info)};
		const DWORD Thread = GetWindowThreadProcessId(Window, nullptr);
		HWND Target = GetGUIThreadInfo(Thread, &Info) && Info.hwndFocus ? Info.hwndFocus : Window;
		if (Type == uint8_t(TSHostProtocol::EKeyType::Char))
		{
			if (Character) PostMessageW(Target, WM_CHAR, WPARAM(Character), 1);
		}
		else
		{
			const bool bUp = Type == uint8_t(TSHostProtocol::EKeyType::KeyUp);
			const LPARAM Bits = LPARAM(1) | (LPARAM(Native) & 0x00FF0000) | (bUp ? (LPARAM(3) << 30) : 0);
			PostMessageW(Target, bUp ? WM_KEYUP : WM_KEYDOWN, WPARAM(KeyCode), Bits);
		}
		break;
	}
	case EInput::Text:
	{
		const std::wstring Text = Widen(R.Str());
		if (!R.Ok) break;
		GUITHREADINFO Info{sizeof(Info)};
		const DWORD Thread = GetWindowThreadProcessId(Window, nullptr);
		HWND Target = GetGUIThreadInfo(Thread, &Info) && Info.hwndFocus ? Info.hwndFocus : Window;
		for (wchar_t Character : Text) PostMessageW(Target, WM_CHAR, WPARAM(Character), 1);
		break;
	}
	default:
		break;
	}
	SetThreadDpiAwarenessContext(Previous);
}
