#define _WIN32_WINNT _WIN32_WINNT_WIN2K
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE	/* use C interface mode to access vtable. */
#define INITGUID	/* KB130869 */
#include <Windows.h>
#include <Ks.h>							// for NANOSECONDS, KSCONVERT_PERFORMANCE_TIME
#include <Shlwapi.h>					// for PathAppend
#pragma region =include <d3d9.h>
#define Direct3DCreate9Ex BrokenDirect3DCreate9Ex
#define IDirect3D9Ex      BrokenIDirect3D9Ex
#define IDirect3D9ExVtbl  BrokenIDirect3D9ExVtbl
#include <d3d9.h>
#undef Direct3DCreate9Ex
#undef IDirect3D9Ex
#undef IDirect3D9ExVtbl

// copy from d3d9.h, and add missing IDirect3D9::RegisterSoftwareDevice().
#undef INTERFACE
#define INTERFACE IDirect3D9Ex
DECLARE_INTERFACE_(IDirect3D9Ex, IDirect3D9) {
	/*** IUnknown methods ***/
	STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj) PURE;
	STDMETHOD_(ULONG, AddRef)(THIS) PURE;
	STDMETHOD_(ULONG, Release)(THIS) PURE;

	/*** IDirect3D9 methods ***/
	STDMETHOD(RegisterSoftwareDevice)(THIS_ void* pInitializeFunction) PURE;
	STDMETHOD_(UINT, GetAdapterCount)(THIS) PURE;
	STDMETHOD(GetAdapterIdentifier)(THIS_ UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) PURE;
	STDMETHOD_(UINT, GetAdapterModeCount)(THIS_ UINT Adapter, D3DFORMAT Format) PURE;
	STDMETHOD(EnumAdapterModes)(THIS_ UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) PURE;
	STDMETHOD(GetAdapterDisplayMode)(THIS_ UINT Adapter, D3DDISPLAYMODE* pMode) PURE;
	STDMETHOD(CheckDeviceType)(THIS_ UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) PURE;
	STDMETHOD(CheckDeviceFormat)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) PURE;
	STDMETHOD(CheckDeviceMultiSampleType)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) PURE;
	STDMETHOD(CheckDepthStencilMatch)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) PURE;
	STDMETHOD(CheckDeviceFormatConversion)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) PURE;
	STDMETHOD(GetDeviceCaps)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) PURE;
	STDMETHOD_(HMONITOR, GetAdapterMonitor)(THIS_ UINT Adapter) PURE;
	STDMETHOD(CreateDevice)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) PURE;
	STDMETHOD_(UINT, GetAdapterModeCountEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter) PURE;
	STDMETHOD(EnumAdapterModesEx)(THIS_ UINT Adapter, CONST D3DDISPLAYMODEFILTER* pFilter, UINT Mode, D3DDISPLAYMODEEX* pMode) PURE;
	STDMETHOD(GetAdapterDisplayModeEx)(THIS_ UINT Adapter, D3DDISPLAYMODEEX* pMode, D3DDISPLAYROTATION* pRotation) PURE;
	STDMETHOD(CreateDeviceEx)(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface) PURE;
	STDMETHOD(GetAdapterLUID)(THIS_ UINT Adapter, LUID * pLUID) PURE;
};
#pragma endregion
#include <array>						// for std::array
#include <type_traits>					// for std::remove_pointer_t
#include <crtdbg.h>						// for _ASSERTE
#pragma comment(lib, "Shlwapi.lib")

namespace {
	template<typename Interface>
	struct vtable {
		typedef std::remove_pointer_t<decltype(Interface::lpVtbl)> type;
	};

	template<typename Interface>
	using vtable_t = typename vtable<Interface>::type;

	HMODULE loadD3d9() {
		static HMODULE d3d9 = []() {
			std::array<TCHAR, MAX_PATH> path;
			GetSystemDirectory(path.data(), static_cast<UINT>(path.size()));
			PathAppend(path.data(), TEXT("d3d9.dll"));
			return LoadLibrary(path.data());
		}();
		_ASSERTE(d3d9);
		return d3d9;
	}

	template<typename Func>
	inline Func* proc(const char* name) {
		auto func = reinterpret_cast<Func*>(GetProcAddress(loadD3d9(), name));
		_ASSERTE(func);
		return func;
	}
	#define PROC(NAME) proc<decltype(NAME)>(#NAME)

	template<typename Fn>
	void update(Fn*& target, Fn*& backup, Fn* hook) {
		if (target == backup)
			return;
		_ASSERTE(!backup);
		MEMORY_BASIC_INFORMATION info;
		auto size = VirtualQuery(&target, &info, sizeof MEMORY_BASIC_INFORMATION);
		_ASSERTE(0 < size);
		auto protect = info.Protect & ~0xFF | (info.Protect & 0x0F ? PAGE_READWRITE : PAGE_EXECUTE_READWRITE);
		auto result = VirtualProtect(&target, sizeof(Fn*), protect, &protect);
		_ASSERTE(result);
		backup = target;
		target = hook;
	}

	void (STDMETHODCALLTYPE *Frame)(ULONGLONG, IDirect3DSurface9*) = nullptr;
	D3DSURFACE_DESC desc;
	auto frequency = []() {
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		return static_cast<ULONGLONG>(frequency.QuadPart);
	}();
	unsigned fps;

	decltype(vtable_t<IDirect3DDevice9>::EndScene) endScene = nullptr;
	HRESULT STDMETHODCALLTYPE EndScene(IDirect3DDevice9* This) {
		static unsigned count = 0;
		auto ticks = []() {
			LARGE_INTEGER value;
			QueryPerformanceCounter(&value);
			return KSCONVERT_PERFORMANCE_TIME(frequency, value);
		}();
		static auto start = [This, &ticks]() {
			IDirect3DSurface9* renderTarget;
			IDirect3DDevice9_GetRenderTarget(This, 0, &renderTarget);
			IDirect3DSurface9_GetDesc(renderTarget, &desc);
			IDirect3DSurface9_Release(renderTarget);
			// quick hack, first 'ticks - start' is 1.
			return ticks++;
		}();
		fps = static_cast<unsigned>(static_cast<ULONGLONG>(++count) * NANOSECONDS / (ticks - start));

		auto result = endScene(This);
		auto frame = Frame;
		if (frame) {
			IDirect3DSurface9* renderTarget;
			IDirect3DDevice9_GetRenderTarget(This, 0, &renderTarget);
			IDirect3DSurface9* offsecreen;
			IDirect3DDevice9_CreateOffscreenPlainSurface(This, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &offsecreen, nullptr);
			IDirect3DDevice9_GetRenderTargetData(This, renderTarget, offsecreen);
			IDirect3DSurface9_Release(renderTarget);
			frame(ticks, offsecreen);
			IDirect3DSurface9_Release(offsecreen);
		}
		return result;
	}

	decltype(vtable_t<IDirect3D9>::CreateDevice) createDevice = nullptr;
	HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9* This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
		auto result = createDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
		if (result == S_OK)
			update((*ppReturnedDeviceInterface)->lpVtbl->EndScene, endScene, EndScene);
		return result;
	}

	decltype(vtable_t<IDirect3D9Ex>::CreateDeviceEx) createDeviceEx = nullptr;
	HRESULT STDMETHODCALLTYPE CreateDeviceEx(IDirect3D9Ex* This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface) {
		auto result = createDeviceEx(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, ppReturnedDeviceInterface);
		if (result == S_OK)
			update(reinterpret_cast<IDirect3DDevice9*>(*ppReturnedDeviceInterface)->lpVtbl->EndScene, endScene, EndScene);
		return result;
	};
}

void STDMETHODCALLTYPE GetParameter(UINT* width, UINT* height, D3DFORMAT* format, unsigned* fps) {
	*width = desc.Width;
	*height = desc.Height;
	*format = desc.Format;
	*fps = ::fps;
}

void STDMETHODCALLTYPE Start(decltype(Frame) frame) {
	Frame = frame;
}

void STDMETHODCALLTYPE Stop() {
	Frame = nullptr;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
	return TRUE;
}

extern "C"{
	int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
		static auto func = PROC(D3DPERF_BeginEvent);
		return func ? func(col, wszName) : -1;
	}
	int WINAPI D3DPERF_EndEvent() {
		static auto func = PROC(D3DPERF_EndEvent);
		return func ? func() : -1;
	}
	DWORD WINAPI D3DPERF_GetStatus() {
		static auto func = PROC(D3DPERF_GetStatus);
		return func ? func() : 0;
	}
	BOOL WINAPI D3DPERF_QueryRepeatFrame() {
		static auto func = PROC(D3DPERF_QueryRepeatFrame);
		return func ? func() : FALSE;
	}
	void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
		static auto func = PROC(D3DPERF_SetMarker);
		if (func) func(col, wszName);
	}
	void WINAPI D3DPERF_SetOptions(DWORD dwOptions) {
		static auto func = PROC(D3DPERF_SetOptions);
		if (func) func(dwOptions);
	}
	void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
		static auto func = PROC(D3DPERF_SetRegion);
		if (func) func(col, wszName);
	}
	int WINAPI DebugSetLevel() {
		static auto func = PROC(DebugSetLevel);
		return func ? func() : -1;
	}
	void WINAPI DebugSetMute() {
		static auto func = PROC(DebugSetMute);
		if (func) func();
	}
	int WINAPI Direct3D9EnableMaximizedWindowedModeShim() {
		static auto func = PROC(Direct3D9EnableMaximizedWindowedModeShim);
		return func ? func() : -1;
	}
	IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
		static auto func = PROC(Direct3DCreate9);
		if (!func)
			return nullptr;
		auto pD3D = func(SDKVersion);
		if (pD3D)
			update(pD3D->lpVtbl->CreateDevice, createDevice, CreateDevice);
		return pD3D;
	}
	HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D) {
		static auto func = PROC(Direct3DCreate9Ex);
		if (!func)
			return D3DERR_NOTAVAILABLE;
		auto result = func(SDKVersion, ppD3D);
		if (result == S_OK) {
			auto pD3DEx = *ppD3D;
			update(reinterpret_cast<IDirect3D9*>(pD3DEx)->lpVtbl->CreateDevice, createDevice, CreateDevice);
			update(pD3DEx->lpVtbl->CreateDeviceEx, createDeviceEx, CreateDeviceEx);
		}
		return result;
	}
}
struct IDirect3DShaderValidator9* __cdecl Direct3DShaderValidatorCreate9() {
	static auto func = PROC(Direct3DShaderValidatorCreate9);
	return func ? func() : nullptr;
}
void __cdecl PSGPError(class D3DFE_PROCESSVERTICES* a, enum PSGPERRORID b, unsigned int c) {
	static auto func = PROC(PSGPError);
	if (func) func(a, b, c);
}
void __cdecl PSGPSampleTexture(class D3DFE_PROCESSVERTICES* a, unsigned int b, float(*const c)[4], unsigned int d, float(*const e)[4]) {
	static auto func = PROC(PSGPSampleTexture);
	if (func) func(a, b, c, d, e);
}
void __cdecl Direct3D9ForceHybridEnumeration(unsigned int a) {
	static auto func = proc<decltype(Direct3D9ForceHybridEnumeration)>(MAKEINTRESOURCEA(16));
	if (func) func(a);
}
