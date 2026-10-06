#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hfr {

namespace {
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
std::wstring g_sessionDir;
int64_t g_t0 = 0;
} // namespace

const std::wstring& SessionDir() { return g_sessionDir; }

void LogInit(const std::wstring& baseDir) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring root = baseDir + L"\\hfr_logs";
    CreateDirectoryW(root.c_str(), nullptr);
    g_sessionDir = root + L"\\" + stamp;
    CreateDirectoryW(g_sessionDir.c_str(), nullptr);
    g_t0 = QpcNow();
    _wfopen_s(&g_logFile, (g_sessionDir + L"\\hfr.log").c_str(), L"wt");
}

void Log(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    double t = QpcToMs(QpcNow() - g_t0) / 1000.0;
    std::lock_guard lock(g_logMutex);
    if (g_logFile) {
        fprintf(g_logFile, "[%9.3f] [%5lu] %s\n", t, GetCurrentThreadId(), buf);
        fflush(g_logFile);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

} // namespace hfr
