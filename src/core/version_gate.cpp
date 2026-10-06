#include "common.h"

#include <cstring>

namespace hfr {

namespace {

struct Expect {
    uintptr_t va;
    const char* hex;
    const char* what;
};

// Plaintext bytes of the supported exe (identical on disk and in memory).
constexpr Expect kExpect[] = {
    {0x0089F0D0, "55 8B EC 83 EC 10 C7 05 74 26 D9 00 00 00 00 00 C7 45 F4 E0 21 C3 00", "DXFreezer loader"},
    {0x00BA0C14, "89 88 08 3D", "f32 1/30"},
    {0x00BA0D20, "89 88 88 3C", "f32 1/60"},
    {0x00C321E0, "44 58 46 72 65 65 7A 65 72 53 65 72 76 65 72 2E 64 6C 6C 00", "\"DXFreezerServer.dll\""},
};

size_t ParseHex(const char* hex, uint8_t* out, size_t cap) {
    size_t n = 0;
    while (*hex && n < cap) {
        while (*hex == ' ') ++hex;
        if (!*hex) break;
        char b[3] = {hex[0], hex[1], 0};
        out[n++] = static_cast<uint8_t>(strtoul(b, nullptr, 16));
        hex += 2;
    }
    return n;
}

// Streaming CRC32: start with crc = 0, feed chunks, result is the standard CRC32 of the concatenation.
uint32_t Crc32Update(uint32_t crc, const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace

bool g_patchingAllowed = false;

bool VersionGateOk() {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != kImageBase) {
        Log("VERSION GATE: main module at %08X, expected %08X", static_cast<unsigned>(base), static_cast<unsigned>(kImageBase));
        return false;
    }
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const unsigned sizeOfImage = nt->OptionalHeader.SizeOfImage;
    Log("Game image: TimeDateStamp=%08X SizeOfImage=%08X%s", static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
        sizeOfImage,
        sizeOfImage == kRuntimeSizeOfImage ? " (DRM-restored runtime header)" : sizeOfImage == kSizeOfImage ? " (disk header)" : " (unexpected)");
    // The DRM rewrites the in-memory header; log what the section table looks like now.
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[9] = {};
        memcpy(name, sec[i].Name, 8);
        Log("  section %-8s VA %08X vsize %08X ch %08X", name, static_cast<unsigned>(kImageBase + sec[i].VirtualAddress),
            static_cast<unsigned>(sec[i].Misc.VirtualSize), static_cast<unsigned>(sec[i].Characteristics));
    }
    if (nt->FileHeader.TimeDateStamp != kTimeDateStamp) {
        Log("VERSION GATE: unsupported exe (expected TimeDateStamp %08X)", kTimeDateStamp);
        return false;
    }
    bool ok = true;
    for (const auto& e : kExpect) {
        uint8_t want[64], have[64];
        size_t n = ParseHex(e.hex, want, sizeof(want));
        if (!SafeRead(reinterpret_cast<const void*>(e.va), have, n) || memcmp(want, have, n) != 0) {
            Log("VERSION GATE: bytes at %08X (%s) differ", static_cast<unsigned>(e.va), e.what);
            ok = false;
        }
    }
    // Informational: is the encrypted region decrypted yet? ducon2016's code cave at 0x57921C is int3 padding.
    uint8_t cave[4] = {};
    SafeRead(reinterpret_cast<const void*>(0x0057921C), cave, 4);
    Log("Decryption probe @0057921C: %02X %02X %02X %02X (%s)", cave[0], cave[1], cave[2], cave[3],
        (cave[0] == 0xCC && cave[1] == 0xCC && cave[2] == 0xCC && cave[3] == 0xCC) ? "decrypted padding" : "NOT padding");
    // CRC32 of the (runtime-decrypted) region 0x401000-0x6DD6C0, logged so later builds can gate on it.
    static uint8_t buf[0x10000];
    uint32_t crc = 0;
    bool readable = true;
    for (uintptr_t a = kTextBegin; a < kEncTextEnd; a += sizeof(buf)) {
        size_t n = (kEncTextEnd - a) < sizeof(buf) ? (kEncTextEnd - a) : sizeof(buf);
        if (!SafeRead(reinterpret_cast<const void*>(a), buf, n)) {
            readable = false;
            break;
        }
        crc = Crc32Update(crc, buf, n);
    }
    Log("Decrypted-region CRC32: %08X%s", crc, readable ? "" : " (region not fully readable)");
    // Instrumentation only observes, so it runs whenever the exe identity matches; code patches additionally need
    // every expected byte to match (and each patch re-checks its own bytes).
    g_patchingAllowed = ok;
    Log("Version gate: exe identity OK; %s", ok ? "all expected bytes match -> patching allowed"
                                                 : "expected bytes differ -> OBSERVE-ONLY (no patches)");
    return true;
}

} // namespace hfr
