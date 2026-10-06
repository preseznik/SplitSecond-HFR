// FIX-001: native 60 Hz main loop.
//
// The game's limiter (Tick @0x558880) accumulates measured frame time and runs Update(ticks) once per 1/30 s
// period with 2 ticks of 1/60 s each. We keep the 1/60 s tick (so every tick-counted system keeps its real-time
// speed) and change only the period: 1/60 s with 1 tick. Three operands that read the shared 1/30 literal
// [0xBA0C14] are redirected to a mod-owned period, and the two "2 ticks" immediates become 1.
//
// FIX-002: precise pacing. The limiter sleeps a truncated millisecond count; when our Sleep hook sees the call from
// 0x558947 it instead waits until the exact deadline (high-resolution waitable timer + short spin).
#include <intrin.h>

#include <safetyhook.hpp>

#pragma intrinsic(_ReturnAddress)

#include "../core/common.h"
#include "../core/patchset.h"

namespace hfr {

namespace {

alignas(16) float g_period = 1.0f / 60.0f; // read by the patched limiter instructions
safetyhook::MidHook g_ticksHook;
PatchSet g_fix001("FIX-001 limiter 60 Hz");
std::atomic<int> g_requestedMode{0}; // 0 = none pending, else 30 or 60
int g_mode = 30;
HANDLE g_waitTimer = nullptr;

constexpr uintptr_t kLimiterSleepRet = 0x00558947; // return address of the limiter's Sleep call
constexpr uintptr_t kQpcAtTimerRead = 0x00D72358;  // int64 QPC value of the frame timer's last read
constexpr uintptr_t kLimiterAcc = 0x00D747F4;      // f32 accumulator

void BuildPatches() {
    const auto period = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_period));
    g_fix001.AddOperand(0x005588C8, 0x00BA0C14, period); // movss xmm1, [1/30]   (compare)
    g_fix001.AddOperand(0x00558908, 0x00BA0C14, period); // fld   [1/30]         (sleep amount)
    g_fix001.AddOperand(0x00558953, 0x00BA0C14, period); // subss xmm0, [1/30]   (after sleep)
    g_fix001.Add(0x005588E6, {0x83, 0xC6, 0x02}, {0x83, 0xC6, 0x01});                     // add esi,2 -> 1
    g_fix001.Add(0x0055895C, {0xBE, 0x02, 0x00, 0x00, 0x00}, {0xBE, 0x01, 0x00, 0x00, 0x00}); // mov esi,2 -> 1
}

void SetMode(int mode) {
    bool ok = mode == 60 ? g_fix001.Apply() : g_fix001.Revert();
    if (ok) g_mode = mode;
    Log("Main loop mode -> %d Hz %s", g_mode, ok ? "" : "(FAILED, unchanged)");
}

void WaitUntil(int64_t deadline) {
    const int64_t f = QpcFreq();
    for (;;) {
        int64_t rem = deadline - QpcNow();
        if (rem <= 0) return;
        int64_t remUs = rem * 1000000 / f;
        if (remUs > 1500 && g_waitTimer) {
            LARGE_INTEGER due;
            due.QuadPart = -(remUs - 1000) * 10; // relative, 100 ns units; wake ~1 ms early, then spin
            if (SetWaitableTimer(g_waitTimer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(g_waitTimer, 50);
        } else if (remUs > 300) {
            SwitchToThread();
        } else {
            _mm_pause();
        }
    }
}

// Called from the Sleep hook. Returns true if the sleep was handled here.
bool LimiterOnSleep(uintptr_t caller, DWORD ms) {
    if (!g_cfg.preciseLimiter || caller != kLimiterSleepRet) return false;
    const double period = g_mode == 60 ? g_period : 1.0 / 30.0;
    const float acc = *reinterpret_cast<const float*>(kLimiterAcc);
    const int64_t tRead = *reinterpret_cast<const int64_t*>(kQpcAtTimerRead);
    double remaining = period - acc;
    if (remaining < 0.0) remaining = 0.0;
    if (remaining > period) remaining = period; // sanity: never wait longer than one period
    int64_t deadline = tRead + static_cast<int64_t>(remaining * static_cast<double>(QpcFreq()));
    // Fallback if the timer read looks stale (e.g. data layout surprise): honour the game's own request.
    int64_t now = QpcNow();
    if (deadline - now > QpcFreq() / 10 || tRead > now) deadline = now + static_cast<int64_t>(ms) * QpcFreq() / 1000;
    WaitUntil(deadline);
    return true;
}

// The game's own Sleep import (original IAT slot); everything except the limiter's call passes straight through.
using Sleep_t = void(WINAPI*)(DWORD);
Sleep_t o_Sleep = nullptr;
void WINAPI h_Sleep(DWORD ms) {
    if (!LimiterOnSleep(reinterpret_cast<uintptr_t>(_ReturnAddress()), ms)) o_Sleep(ms);
}

// 0x558984 runs right after the limiter clamped the tick count (esi) and before Update(ticks).
void OnTicks(safetyhook::Context& ctx) {
    g_ticks = static_cast<int>(ctx.esi);
    g_frameDt = static_cast<float>(ctx.esi) * (1.0f / 60.0f);
    FixAi30OnTicks(g_ticks);
}

} // namespace

// Simulation time advanced this frame (ticks / 60). Equals 1/30 in vanilla 30 Hz mode, so per-frame code that
// hard-codes 1/30 can be redirected here and stays bit-identical at 30 Hz.
alignas(16) float g_frameDt = 1.0f / 30.0f;
int g_ticks = 2;

void LimiterInit() {
    BuildPatches();
    g_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (g_cfg.preciseLimiter &&
        !HookIatSlot("KERNEL32.dll", "Sleep", reinterpret_cast<void*>(&h_Sleep), reinterpret_cast<void**>(&o_Sleep))) {
        Log("Limiter: Sleep hook failed, precise pacing disabled");
        g_cfg.preciseLimiter = false;
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(0x00558984), &OnTicks)) g_ticksHook = std::move(*h);
    else Log("Limiter: ticks hook FAILED (frame-dt fixes will use 1/30)");
    std::string why;
    Log("Limiter: FIX-001 %s; precise pacing %s; requested mode %d Hz", g_fix001.Verify(&why) ? "verified" : why.c_str(),
        g_cfg.preciseLimiter ? "on" : "off", g_cfg.mode);
    g_requestedMode = g_cfg.mode == 60 ? 60 : 0; // applied on the first frame boundary
}

int LimiterMode() { return g_mode; }

void LimiterRequestToggle() { g_requestedMode = g_mode == 60 ? 30 : 60; }

// Main thread, at a frame boundary (after Present returned; the patched instructions are not executing).
void LimiterFrame() {
    int req = g_requestedMode.exchange(0);
    if (req && req != g_mode) SetMode(req);
}

} // namespace hfr
