// Per-Present bookkeeping on the main thread: the 30 <-> 60 hotkey, the limiter's frame-boundary work, and a
// background writer that logs an fps summary every few seconds (plus frames.csv if FrameLog=1).
#include <cstdio>

#include "common.h"
#if HFR_DEVTOOLS
#include "../instr/stats.h"
#endif

namespace hfr {

namespace {

struct FrameRec {
    uint32_t idx;
    int64_t tEnter, tExit;
};
constexpr uint32_t kRing = 1 << 14;
FrameRec g_ring[kRing];
std::atomic<uint32_t> g_written{0}; // producer: main thread
uint32_t g_frame = 0;
int64_t g_t0 = 0;
bool g_keyPrev[256] = {};

bool OurWindowIsForeground() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool KeyPressed(int vk) {
    if (vk <= 0 || vk > 255) return false;
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = down && !g_keyPrev[vk];
    g_keyPrev[vk] = down;
    return edge;
}

DWORD WINAPI StatsWriterThread(void*) {
    FILE* frames = nullptr;
    if (g_cfg.frameLog) {
        _wfopen_s(&frames, (SessionDir() + L"\\frames.csv").c_str(), L"wt");
        if (frames) fprintf(frames, "frame,t_ms,frame_ms,present_ms\n");
    }
    uint32_t read = 0;
    int64_t prevEnter = 0;
#if HFR_DEVTOOLS
    uint32_t seenMarker = 0;
#endif
    for (;;) {
        Sleep(static_cast<DWORD>(g_cfg.statsIntervalSec) * 1000);
        uint32_t w = g_written.load(std::memory_order_acquire);
        if (w - read > kRing) read = w - kRing; // overrun: skip
        double sum = 0, mx = 0, mn = 1e9, psum = 0;
        uint32_t cnt = 0;
        for (; read != w; ++read) {
            const FrameRec& r = g_ring[read & (kRing - 1)];
            double fms = prevEnter ? QpcToMs(r.tEnter - prevEnter) : 0.0;
            double pms = QpcToMs(r.tExit - r.tEnter);
            prevEnter = r.tEnter;
            if (frames) fprintf(frames, "%u,%.3f,%.3f,%.3f\n", r.idx, QpcToMs(r.tEnter - g_t0), fms, pms);
            if (fms > 0) {
                sum += fms;
                psum += pms;
                mx = fms > mx ? fms : mx;
                mn = fms < mn ? fms : mn;
                ++cnt;
            }
        }
        if (frames) fflush(frames);
        if (cnt) Log("frames [%d Hz mode]: %u in %ds, avg %.2f ms (%.1f fps), min %.2f, max %.2f, avg present %.2f ms",
                     LimiterMode(), cnt, g_cfg.statsIntervalSec, sum / cnt, 1000.0 * cnt / sum, mn, mx, psum / cnt);
#if HFR_DEVTOOLS
        if (g_cfg.probes) ReportProbes(g_cfg.statsIntervalSec, cnt);
        if (g_cfg.iatHooks || g_cfg.probes || g_cfg.sampler) LogMemoryStats();
        if (g_cfg.iatHooks) {
            stats::WriteApiSnapshot("");
            uint32_t gen = stats::g_markerGen.load();
            if (gen != seenMarker) {
                seenMarker = gen;
                char sfx[32];
                snprintf(sfx, sizeof(sfx), "_m%u", gen);
                stats::WriteApiSnapshot(sfx);
            }
        }
#endif
    }
}

} // namespace

void StartStatsWriter() {
    g_t0 = QpcNow();
    HANDLE h = CreateThread(nullptr, 0, StatsWriterThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

#if HFR_DEVTOOLS
void AddMarker(const char* what) {
    uint32_t gen = stats::g_markerGen.fetch_add(1) + 1;
    Log("MARKER %u (%s) at frame %u", gen, what, g_frame);
}
#endif

// Called on the main thread from the Present hook, after the real Present returned.
void FrameTick(HWND, int64_t tEnter, int64_t tExit) {
    ++g_frame;
    if (g_frame == 1) {
        Log("First Present on thread %lu", GetCurrentThreadId());
#if HFR_DEVTOOLS
        stats::g_renderTid.store(GetCurrentThreadId());
#endif
    }
    uint32_t w = g_written.load(std::memory_order_relaxed);
    g_ring[w & (kRing - 1)] = {g_frame, tEnter, tExit};
    g_written.store(w + 1, std::memory_order_release);

    if (OurWindowIsForeground()) {
        if (KeyPressed(g_cfg.keyToggle)) LimiterRequestToggle();
#if HFR_DEVTOOLS
        if (KeyPressed(g_cfg.keyDump)) RequestDump("hotkey");
        if (KeyPressed(g_cfg.keyMarker)) AddMarker("hotkey");
#endif
    }
    LimiterFrame();
#if HFR_DEVTOOLS
    if (g_cfg.autoDump && g_frame == static_cast<uint32_t>(g_cfg.autoDumpFrames)) RequestDump("frames");
#endif
}

} // namespace hfr
