#include "common.h"

namespace hfr {

Config g_cfg;

namespace {
int ReadInt(const std::wstring& ini, const wchar_t* sec, const wchar_t* key, int def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini.c_str());
    if (!buf[0]) return def;
    return static_cast<int>(wcstol(buf, nullptr, 0)); // accepts 0x.. for virtual-key codes
}
} // namespace

void LoadConfig(const std::wstring& ini) {
    Config& c = g_cfg;
    c.enabled = ReadInt(ini, L"General", L"Enabled", c.enabled) != 0;
    c.iatHooks = ReadInt(ini, L"Instrumentation", L"IatHooks", c.iatHooks) != 0;
    c.sampler = ReadInt(ini, L"Instrumentation", L"Sampler", c.sampler) != 0;
    c.probes = ReadInt(ini, L"Instrumentation", L"Probes", c.probes) != 0;
    c.samplerHz = ReadInt(ini, L"Instrumentation", L"SamplerHz", c.samplerHz);
    c.frameLog = ReadInt(ini, L"Logging", L"FrameLog", ReadInt(ini, L"Instrumentation", L"FrameLog", c.frameLog)) != 0;
    c.autoDump = ReadInt(ini, L"Instrumentation", L"AutoDump", c.autoDump) != 0;
    c.autoDumpFrames = ReadInt(ini, L"Instrumentation", L"AutoDumpFrames", c.autoDumpFrames);
    c.statsIntervalSec = ReadInt(ini, L"Instrumentation", L"StatsIntervalSec", c.statsIntervalSec);
    c.crashHandler = ReadInt(ini, L"Instrumentation", L"CrashHandler", c.crashHandler) != 0;
    c.mode = ReadInt(ini, L"Timing", L"Mode", c.mode) == 30 ? 30 : 60;
    c.preciseLimiter = ReadInt(ini, L"Timing", L"PreciseLimiter", c.preciseLimiter) != 0;
    c.fixAi30 = ReadInt(ini, L"Fixes", L"AI30Hz", c.fixAi30) != 0;
    c.keyDump = ReadInt(ini, L"Hotkeys", L"Dump", c.keyDump);
    c.keyToggle = ReadInt(ini, L"Hotkeys", L"ToggleFps", c.keyToggle);
    c.keyMarker = ReadInt(ini, L"Hotkeys", L"Marker", c.keyMarker);
    if (c.samplerHz < 10) c.samplerHz = 10;
    if (c.samplerHz > 4000) c.samplerHz = 4000;
    if (c.statsIntervalSec < 1) c.statsIntervalSec = 1;
}

} // namespace hfr
