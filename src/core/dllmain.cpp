#include "common.h"

namespace hfr {
HMODULE g_self = nullptr;
}

// Real initialisation happens in dxfreezer_Direct3DCreate9 (src/d3d/d3d9_hooks.cpp): it runs outside the loader lock
// and after SecuROM/SteamStub have decrypted the game code.
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hfr::g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
