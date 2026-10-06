#include "patchset.h"

#include <cstring>

#include "common.h"

namespace hfr {

void PatchSet::Add(uintptr_t va, std::initializer_list<uint8_t> expect, std::initializer_list<uint8_t> repl) {
    patches_.push_back({va, std::vector<uint8_t>(expect), std::vector<uint8_t>(repl)});
}

void PatchSet::AddOperand(uintptr_t va, uint32_t expectOld, uint32_t newValue) {
    Patch p{va, std::vector<uint8_t>(4), std::vector<uint8_t>(4)};
    memcpy(p.expect.data(), &expectOld, 4);
    memcpy(p.repl.data(), &newValue, 4);
    patches_.push_back(std::move(p));
}

bool PatchSet::Verify(std::string* why) const {
    for (const auto& p : patches_) {
        std::vector<uint8_t> cur(p.expect.size());
        if (!SafeRead(reinterpret_cast<const void*>(p.va), cur.data(), cur.size())) {
            if (why) *why = "unreadable";
            return false;
        }
        const auto& want = applied_ ? p.repl : p.expect;
        if (cur != want) {
            if (why) {
                char buf[64];
                snprintf(buf, sizeof(buf), "bytes at %08X differ", static_cast<unsigned>(p.va));
                *why = buf;
            }
            return false;
        }
    }
    return true;
}

bool PatchSet::Write(uintptr_t va, const std::vector<uint8_t>& bytes) {
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(va), bytes.size(), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(reinterpret_cast<void*>(va), bytes.data(), bytes.size());
    VirtualProtect(reinterpret_cast<void*>(va), bytes.size(), old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(va), bytes.size());
    return true;
}

bool PatchSet::Apply() {
    if (applied_) return true;
    if (!g_patchingAllowed) {
        Log("Patch %s: patching disabled by version gate", name_.c_str());
        return false;
    }
    std::string why;
    if (!Verify(&why)) {
        Log("Patch %s: NOT applied (%s)", name_.c_str(), why.c_str());
        return false;
    }
    for (const auto& p : patches_) Write(p.va, p.repl);
    applied_ = true;
    return true;
}

bool PatchSet::Revert() {
    if (!applied_) return true;
    std::string why;
    if (!Verify(&why)) {
        Log("Patch %s: NOT reverted (%s)", name_.c_str(), why.c_str());
        return false;
    }
    for (const auto& p : patches_) Write(p.va, p.expect);
    applied_ = false;
    return true;
}

} // namespace hfr
