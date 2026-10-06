// Hooks on the game's ORIGINAL import table (0xB9B000..). Only calls made by game code through its own IAT are
// intercepted; system DLLs are not patched. Each slot is verified to hold the real API before it is swapped.
#include "common.h"

namespace hfr {

namespace {

// Original-IAT slots, from the descriptors on disk. Hard-coded rather than parsed at runtime because the DRM
// rewrites headers in memory; each slot's content is still verified before it is swapped.
struct SlotInfo {
    const char* dll;
    const char* name;
    uintptr_t slot;
};
constexpr SlotInfo kSlots[] = {
    {"KERNEL32.dll", "QueryPerformanceCounter", 0x00B9B104},
    {"KERNEL32.dll", "QueryPerformanceFrequency", 0x00B9B108},
    {"KERNEL32.dll", "WaitForSingleObject", 0x00B9B11C},
    {"KERNEL32.dll", "Sleep", 0x00B9B120},
    {"KERNEL32.dll", "GetTickCount", 0x00B9B168},
    {"KERNEL32.dll", "WaitForMultipleObjects", 0x00B9B1D0},
    {"WINMM.dll", "timeGetTime", 0x00B9B2D4},
};

} // namespace

bool HookIatSlot(const char* dll, const char* name, void* hook, void** orig) {
    uintptr_t slot = 0;
    for (const auto& s : kSlots)
        if (_stricmp(s.dll, dll) == 0 && strcmp(s.name, name) == 0) slot = s.slot;
    if (!slot || slot < kOrigIatBegin || slot >= kOrigIatEnd) {
        Log("IAT: %s!%s slot not known", dll, name);
        return false;
    }
    void* cur = nullptr;
    if (!SafeRead(reinterpret_cast<const void*>(slot), &cur, sizeof(cur))) {
        Log("IAT: %s!%s slot %08X not readable", dll, name, static_cast<unsigned>(slot));
        return false;
    }
    HMODULE m = GetModuleHandleA(dll);
    void* real = m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr;
    if (cur != real) {
        // Leave DRM-redirected or otherwise unexpected slots alone.
        Log("IAT: %s!%s slot %08X holds %s (real %p) -> NOT hooked", dll, name, static_cast<unsigned>(slot),
            DescribeAddress(reinterpret_cast<uintptr_t>(cur)).c_str(), real);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(slot), 4, PAGE_READWRITE, &old)) return false;
    *orig = cur;
    *reinterpret_cast<void**>(slot) = hook;
    VirtualProtect(reinterpret_cast<void*>(slot), 4, old, &old);
    Log("IAT: hooked %s!%s at slot %08X", dll, name, static_cast<unsigned>(slot));
    return true;
}

} // namespace hfr
