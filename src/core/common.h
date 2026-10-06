#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace hfr {

// ---- Facts about the supported game image (Steam build 385319, SplitSecond.exe 1.0.0.1) -------------------------
constexpr uintptr_t kImageBase = 0x00400000;
// The on-disk header says 0x1DB7000 (incl. DRM sections). At runtime the DRM restores the inner image's header,
// which says 0xE1B000 (ends where Stext begins). The mapping itself still spans the disk size.
constexpr uint32_t kSizeOfImage = 0x01DB7000;
constexpr uint32_t kRuntimeSizeOfImage = 0x00E1B000;
constexpr uint32_t kTimeDateStamp = 0x4BE13C66;
constexpr uintptr_t kTextBegin = 0x00401000;
constexpr uintptr_t kTextEnd = 0x00B9AF91;    // end of .text VirtualSize
constexpr uintptr_t kEncTextEnd = 0x006DD6C0; // the first 3,000,000 bytes of .text are encrypted on disk
constexpr uintptr_t kOrigIatBegin = 0x00B9B000;
constexpr uintptr_t kOrigIatEnd = 0x00B9B568;
constexpr uint32_t kOrigImportDescRva = 0x008EFFE4;
constexpr uintptr_t kDrmBegin = 0x0118E000; // .bind, Stext, Sitext, Srdata, Sdata, Sidata, .securom
constexpr uintptr_t kDrmEnd = 0x021B7000;
constexpr uintptr_t kSecuromIat = 0x0201BDB4; // live (SecuROM-merged) IAT
constexpr uint32_t kSecuromIatSize = 0x8A0;

inline bool InGameText(uintptr_t a) { return a >= kTextBegin && a < kTextEnd; }
inline bool InGameImage(uintptr_t a) { return a >= kImageBase && a < kImageBase + kSizeOfImage; }
inline bool InDrm(uintptr_t a) { return a >= kDrmBegin && a < kDrmEnd; }

// ---- Config (SplitSecondHFR.ini next to the DLL) -----------------------------------------------------------------
struct Config {
    bool enabled = true;           // false: pure pass-through, game behaves exactly like vanilla
    int mode = 60;                 // main loop: 60 = native 60 Hz (FIX-001), 30 = vanilla
    bool preciseLimiter = true;    // FIX-002: exact frame pacing instead of the limiter's truncated Sleep
    bool fixAi30 = true;           // FIX-003: AI driver update at 30 Hz cadence
    int keyToggle = VK_F8;         // switch 30 <-> 60 at runtime
    bool frameLog = false;         // per-frame timing CSV (frames.csv)
    int statsIntervalSec = 5;      // fps summary line in hfr.log
    // Developer instrumentation (HFR_DEVTOOLS builds only, not in the public source tree). All off by default: some
    // of it makes the game's copy protection degrade the game (missing track geometry).
    bool iatHooks = false;         // swap original-IAT slots of timing APIs and count callers
    bool sampler = false;          // EIP/stack sampler on game threads
    int samplerHz = 1000;
    bool probes = false;           // install [Probes] mid-hooks
    bool autoDump = false;         // dump image shortly after init and after AutoDumpFrames presents
    int autoDumpFrames = 1800;
    bool crashHandler = false;
    int keyDump = 0;               // e.g. VK_F9
    int keyMarker = 0;             // e.g. VK_F10
};
extern Config g_cfg;
void LoadConfig(const std::wstring& iniPath);

// ---- Logging (hfr_logs\<session>\hfr.log) ------------------------------------------------------------------------
void LogInit(const std::wstring& baseDir);
void Log(const char* fmt, ...);
const std::wstring& SessionDir();

// ---- Misc utilities -----------------------------------------------------------------------------------------------
std::wstring ModuleDir(HMODULE m);
int64_t QpcNow();
int64_t QpcFreq();
double QpcToMs(int64_t ticks);
// "module+0xOFFSET" for any address (safe to call from normal threads, not while another thread is suspended)
std::string DescribeAddress(uintptr_t a);
// True if [p, p+n) is committed, readable and has no guard/no-access pages. Uses VirtualQuery only, so it never
// raises an exception (SecuROM's handlers must not see stray access violations or guard-page hits from us).
bool IsReadable(const void* p, size_t n);
// Copy after an IsReadable check; returns false (and copies nothing) if any page is not plainly readable.
bool SafeRead(const void* src, void* dst, size_t n);
std::string Utf8(const std::wstring& w);

extern HMODULE g_self;

// ---- Subsystems ---------------------------------------------------------------------------------------------------
// True if this is the supported exe (the mod may run). Sets g_patchingAllowed if all expected bytes match.
bool VersionGateOk();
extern bool g_patchingAllowed;
// Swap a slot of the game's original import table (src/core/iat.cpp); verifies the slot holds the real API.
bool HookIatSlot(const char* dll, const char* name, void* hook, void** orig);
void StartStatsWriter();
void FrameTick(HWND deviceWindow, int64_t tEnter, int64_t tExit); // called from Present (main thread)

// Main-loop timing (src/timing/limiter.cpp)
void LimiterInit();
int LimiterMode();
void LimiterRequestToggle();
void LimiterFrame();    // main thread, frame boundary
extern float g_frameDt; // ticks/60 of the current frame (1/30 in vanilla mode)
extern int g_ticks;

// Fixes (src/fixes/)
void InstallFixAi30();
void FixAi30OnTicks(int ticks);

// Developer instrumentation (src/instr/, only in HFR_DEVTOOLS builds)
void InstallCrashHandler();
int InstallApiHooks();
void StartSampler();
void RequestDump(const char* label);
void AddMarker(const char* what);
void InstallProbes(const std::wstring& ini);
void ReportProbes(double seconds, uint32_t frames);
void LogMemoryStats();

} // namespace hfr
