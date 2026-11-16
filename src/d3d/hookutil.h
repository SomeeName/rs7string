#pragma once
#include <cstddef>
#include <cstdint>

enum class Rs7Family : uint8_t;  // features/colours/colours.h
struct Rs7MemReader;             // d3d/texlist.h

// Helpers shared by the device-hook modules (swap_*.cpp, present.cpp, worker.cpp): logging that cannot throw into a
// hook, the module that owns an address (for the install log lines), the ramp family names of the log lines and
// the two-slot vtable hook.
namespace rs7d3d {
// Rs7Log that never throws into the hook (std::string allocation inside Rs7Log).
void LogQuiet(const char* fmt, ...);
// File name of the module that contains `addr`, or "no module".
void ModuleName(const void* addr, char* out, size_t n);
// "note", "additive" or "embedded".
const char* FamilyName(Rs7Family f);

// One value per slot of a VtSlotPair: slot `lo` and slot `lo + 1`.
struct SlotValues {
    uintptr_t lo, hi;
};

// Two neighbouring device vtable slots `lo` and `lo + 1` patched with our hot-patchable thunks. `lo` is written
// first and restored last. `orig` holds d3d9's entry points of both slots. Log lines start with `tag`.
struct VtSlotPair {
    const char* tag;
    unsigned lo;
    SlotValues thunk;

    bool Ours(void* const* vt) const;
    // Writes both thunks; restores changed slots if either exchange or protection cleanup fails. Logs a failure.
    bool Write(const void* device, void** vt, SlotValues orig) const;
    void Restore(void** vt, SlotValues orig) const;
    // Repairs pairs containing only originals and our thunks; logs once (`*foreignLogged`) when another
    // module took them.
    void Recheck(const void* device, void** vt, SlotValues orig, const Rs7MemReader& mem, bool* foreignLogged) const;
};
}  // namespace rs7d3d
