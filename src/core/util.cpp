#include "common.h"

#include <cstdio>

namespace hfr {

std::wstring ModuleDir(HMODULE m) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

int64_t QpcNow() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

int64_t QpcFreq() {
    static const int64_t f = [] {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return li.QuadPart;
    }();
    return f;
}

double QpcToMs(int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(QpcFreq()); }

bool IsReadable(const void* p, size_t n) {
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    auto a = reinterpret_cast<uintptr_t>(p);
    const uintptr_t end = a + n;
    if (n == 0 || end < a) return n == 0;
    while (a < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & kReadable) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool SafeRead(const void* src, void* dst, size_t n) {
    if (!IsReadable(src, n)) return false;
    memcpy(dst, src, n);
    return true;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string DescribeAddress(uintptr_t a) {
    char buf[300];
    if (InGameImage(a)) {
        snprintf(buf, sizeof(buf), "game!%08X%s", static_cast<unsigned>(a), InDrm(a) ? "(DRM)" : "");
        return buf;
    }
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(a), &m) && m) {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
        std::wstring s(path, n);
        size_t slash = s.find_last_of(L"\\/");
        std::string name = Utf8(slash == std::wstring::npos ? s : s.substr(slash + 1));
        snprintf(buf, sizeof(buf), "%s+0x%X", name.c_str(), static_cast<unsigned>(a - reinterpret_cast<uintptr_t>(m)));
        return buf;
    }
    snprintf(buf, sizeof(buf), "?%08X", static_cast<unsigned>(a));
    return buf;
}

} // namespace hfr
