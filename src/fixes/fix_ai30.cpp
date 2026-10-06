// FIX-003: run the AI driver at its original 30 Hz cadence.
//
// SSAI::cAIPlayer::Update (vtbl 0xBC0BB4 slot 3 = 0x51A8B0) runs once per frame. Its steering controller keeps a
// history of per-frame error samples (PI(D) window, derivative vs. a sample ~5 frames old) and slew-limits the
// steering by "Max steering velocity" x 1/30 per frame. At 60 FPS that halves the controller's time window and
// doubles the slew rate. Instead of re-tuning it, we let the whole AI update run only when >= 2 ticks (1/30 s) of
// simulation have accumulated — exactly vanilla's cadence (physics still steps every tick in both modes) — and on
// skipped frames re-apply the outputs the AI wrote last time, as vanilla's per-frame AI update would have.
//
// Pure pointer swap in the vtable (no code bytes patched). In 30 Hz mode every frame has 2 ticks, so the original
// runs every frame and behaviour is unchanged.
#include <cstring>

#include "../core/common.h"

namespace hfr {

namespace {

constexpr uintptr_t kAiVtblSlot = 0x00BC0BC0; // cAIPlayer vtable slot 3
constexpr uintptr_t kAiUpdate = 0x0051A8B0;

using AiUpdate_t = void(__thiscall*)(void* self, void* player, uint8_t* controls);
AiUpdate_t o_AiUpdate = nullptr;

int g_tickAcc = 0;
bool g_runThisFrame = true;

struct Saved {
    uint8_t* controls = nullptr;
    uint8_t outputs[0x10] = {}; // controls+0x10 .. +0x1F (brake / ... / throttle / steer)
    uint8_t flagCD = 0;         // controls+0xCD
    bool valid = false;
};
Saved g_saved[16];

Saved* SlotFor(uint8_t* controls) {
    for (auto& s : g_saved)
        if (s.controls == controls) return &s;
    for (auto& s : g_saved)
        if (!s.controls) {
            s.controls = controls;
            return &s;
        }
    return nullptr;
}

void __fastcall AiUpdateGate(void* self, void* /*edx*/, void* player, uint8_t* controls) {
    Saved* s = controls ? SlotFor(controls) : nullptr;
    if (g_runThisFrame || !s || !s->valid) {
        o_AiUpdate(self, player, controls);
        if (s) {
            memcpy(s->outputs, controls + 0x10, sizeof(s->outputs));
            s->flagCD = controls[0xCD];
            s->valid = true;
        }
        return;
    }
    memcpy(controls + 0x10, s->outputs, sizeof(s->outputs));
    controls[0xCD] = s->flagCD;
}

bool SwapSlot(uintptr_t slot, uintptr_t expect, void* repl) {
    uintptr_t cur = 0;
    if (!SafeRead(reinterpret_cast<const void*>(slot), &cur, 4) || cur != expect) {
        Log("FIX-003: vtable slot %08X holds %08X, expected %08X -> not installed", static_cast<unsigned>(slot),
            static_cast<unsigned>(cur), static_cast<unsigned>(expect));
        return false;
    }
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(slot), 4, PAGE_READWRITE, &old);
    *reinterpret_cast<void**>(slot) = repl;
    VirtualProtect(reinterpret_cast<void*>(slot), 4, old, &old);
    return true;
}

} // namespace

// Called once per frame from the limiter (after the tick clamp, before Update).
void FixAi30OnTicks(int ticks) {
    g_tickAcc += ticks;
    if (g_tickAcc >= 2) {
        g_runThisFrame = true;
        g_tickAcc -= 2;
        if (g_tickAcc > 1) g_tickAcc = 1; // no backlog: never run the AI twice in one frame
    } else {
        g_runThisFrame = false;
    }
}

void InstallFixAi30() {
    if (!g_cfg.fixAi30) {
        Log("FIX-003 AI 30 Hz: disabled in INI");
        return;
    }
    if (!g_patchingAllowed) return;
    o_AiUpdate = reinterpret_cast<AiUpdate_t>(kAiUpdate);
    if (SwapSlot(kAiVtblSlot, kAiUpdate, reinterpret_cast<void*>(&AiUpdateGate)))
        Log("FIX-003 AI 30 Hz: installed (cAIPlayer::Update gated to >= 2 ticks)");
}

} // namespace hfr
