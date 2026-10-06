#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace hfr {

// A named group of byte patches that is verified and applied (or reverted) all-or-nothing.
// Every patch records the exact bytes it expects to replace; if any differ, nothing is written.
// Apply/Revert must be called while no thread can be executing the patched instructions — in practice from the
// Present hook on the main thread, which is outside the main loop's Tick (0x558880).
class PatchSet {
public:
    explicit PatchSet(std::string name) : name_(std::move(name)) {}

    void Add(uintptr_t va, std::initializer_list<uint8_t> expect, std::initializer_list<uint8_t> repl);
    // Replace a 32-bit absolute operand (e.g. the address in `movss xmm1, [0xBA0C14]`).
    void AddOperand(uintptr_t va, uint32_t expectOld, uint32_t newValue);

    bool Verify(std::string* why = nullptr) const; // memory currently holds the expected bytes (or is already applied)
    bool Apply();
    bool Revert();
    bool Applied() const { return applied_; }
    const std::string& Name() const { return name_; }

private:
    struct Patch {
        uintptr_t va;
        std::vector<uint8_t> expect, repl;
    };
    static bool Write(uintptr_t va, const std::vector<uint8_t>& bytes);
    std::string name_;
    std::vector<Patch> patches_;
    bool applied_ = false;
};

} // namespace hfr
