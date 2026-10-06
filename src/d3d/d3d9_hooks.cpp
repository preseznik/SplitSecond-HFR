// Entry point: the game's plaintext loader at 0x89F0D0 does LoadLibraryA("DXFreezerServer.dll") and, if the export
// below exists, calls it (cdecl, SDK version) instead of Direct3DCreate9. By then SecuROM/SteamStub have decrypted
// the game code, so this is where everything is initialised.
#include <d3d9.h>

#include <mutex>

#include "../core/common.h"

namespace hfr {

namespace {

#ifndef HFR_VERSION
#define HFR_VERSION "dev"
#endif
constexpr const char* kVersion = HFR_VERSION;
bool g_active = false;
HWND g_devWindow = nullptr;

using CreateDevice_t = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                        IDirect3DDevice9**);
using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

CreateDevice_t o_CreateDevice = nullptr;
Present_t o_Present = nullptr;
Reset_t o_Reset = nullptr;

// vtable slots (d3d9.h declaration order)
constexpr int kD3D9_CreateDevice = 16;
constexpr int kDev_Reset = 16;
constexpr int kDev_Present = 17;

void* SwapVtbl(void* obj, int idx, void* fn) {
    void** vtbl = *reinterpret_cast<void***>(obj);
    if (vtbl[idx] == fn) return nullptr;
    DWORD old;
    VirtualProtect(&vtbl[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* orig = vtbl[idx];
    vtbl[idx] = fn;
    VirtualProtect(&vtbl[idx], sizeof(void*), old, &old);
    return orig;
}

void LogPP(const char* tag, const D3DPRESENT_PARAMETERS* pp) {
    if (!pp) return;
    Log("%s: %ux%u fmt=%d bb=%u msaa=%d/%lu swap=%d hwnd=%p windowed=%d autodepth=%d(%d) flags=%08lX refresh=%u "
        "interval=%08X",
        tag, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount, pp->MultiSampleType,
        pp->MultiSampleQuality, pp->SwapEffect, static_cast<void*>(pp->hDeviceWindow), pp->Windowed,
        pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags, pp->FullScreen_RefreshRateInHz,
        pp->PresentationInterval);
}

HRESULT WINAPI h_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
    int64_t t0 = QpcNow();
    HRESULT hr = o_Present(dev, src, dst, wnd, dirty);
    FrameTick(g_devWindow, t0, QpcNow());
    return hr;
}

HRESULT WINAPI h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    LogPP("Reset", pp);
    HRESULT hr = o_Reset(dev, pp);
    Log("Reset -> %08lX", hr);
    return hr;
}

HRESULT WINAPI h_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                              D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    D3DDISPLAYMODE dm{};
    if (SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &dm)))
        Log("Adapter %u display mode: %ux%u @ %u Hz fmt=%d", adapter, dm.Width, dm.Height, dm.RefreshRate, dm.Format);
    LogPP("CreateDevice", pp);
    Log("CreateDevice: adapter=%u type=%d focus=%p behavior=%08lX", adapter, type, static_cast<void*>(focus), behavior);
    HRESULT hr = o_CreateDevice(d3d, adapter, type, focus, behavior, pp, out);
    Log("CreateDevice -> %08lX", hr);
    if (SUCCEEDED(hr) && out && *out) {
        g_devWindow = pp && pp->hDeviceWindow ? pp->hDeviceWindow : focus;
        if (void* p = SwapVtbl(*out, kDev_Present, reinterpret_cast<void*>(&h_Present))) o_Present = reinterpret_cast<Present_t>(p);
        if (void* p = SwapVtbl(*out, kDev_Reset, reinterpret_cast<void*>(&h_Reset))) o_Reset = reinterpret_cast<Reset_t>(p);
        Log("Device hooks: Present=%p Reset=%p", reinterpret_cast<void*>(o_Present), reinterpret_cast<void*>(o_Reset));
    }
    return hr;
}

void InitOnce() {
    std::wstring dir = ModuleDir(g_self);
    LoadConfig(dir + L"\\SplitSecondHFR.ini");
    LogInit(dir);
    Log("SplitSecondHFR %s (built %s %s) loaded via the game's DXFreezer hook", kVersion, __DATE__, __TIME__);
    Log("Command line: %s", GetCommandLineA());
    if (!g_cfg.enabled) {
        Log("Enabled=0 -> pass-through, nothing hooked");
        return;
    }
#if HFR_DEVTOOLS
    if (g_cfg.crashHandler) InstallCrashHandler();
#endif
    if (!VersionGateOk()) return;
    g_active = true;
#if HFR_DEVTOOLS
    // Instrumentation only on request: some of it makes the game's copy protection degrade the game.
    int iat = g_cfg.iatHooks ? InstallApiHooks() : 0;
#endif
    LimiterInit(); // FIX-001 (60 Hz limiter), FIX-002 (precise pacing: Sleep slot), tick capture
    InstallFixAi30();
    StartStatsWriter();
#if HFR_DEVTOOLS
    if (g_cfg.probes) InstallProbes(dir + L"\\SplitSecondHFR.ini");
    if (g_cfg.sampler) StartSampler();
    if (g_cfg.autoDump) RequestDump("init");
    Log("Dev tools: IAT instrumentation %d slots, probes %s, sampler %s, crash handler %s, auto-dump %s", iat,
        g_cfg.probes ? "on" : "off", g_cfg.sampler ? "on" : "off", g_cfg.crashHandler ? "on" : "off",
        g_cfg.autoDump ? "on" : "off");
#endif
    Log("Active: mode %d Hz, precise pacing %s, AI 30 Hz %s, frame log %s, patching %s", g_cfg.mode,
        g_cfg.preciseLimiter ? "on" : "off", g_cfg.fixAi30 ? "on" : "off", g_cfg.frameLog ? "on" : "off",
        g_patchingAllowed ? "allowed" : "disabled");
}

} // namespace

} // namespace hfr

extern "C" IDirect3D9* __cdecl dxfreezer_Direct3DCreate9(UINT sdk) {
    using namespace hfr;
    static std::once_flag once;
    std::call_once(once, InitOnce);

    using Create_t = IDirect3D9*(WINAPI*)(UINT);
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d3d9) d3d9 = LoadLibraryW(L"d3d9.dll");
    auto real = d3d9 ? reinterpret_cast<Create_t>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    IDirect3D9* d3d = real ? real(sdk) : nullptr;
    if (g_cfg.enabled) Log("Direct3DCreate9(0x%X) -> %p (d3d9 at %p)", sdk, static_cast<void*>(d3d), static_cast<void*>(d3d9));
    if (d3d && g_active) {
        if (void* p = SwapVtbl(d3d, kD3D9_CreateDevice, reinterpret_cast<void*>(&h_CreateDevice)))
            o_CreateDevice = reinterpret_cast<CreateDevice_t>(p);
    }
    return d3d;
}
