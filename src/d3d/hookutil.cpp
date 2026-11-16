#include "d3d/hookutil.h"
#include "d3d/texlist.h"
#include "features/colours/colours.h"
#include "platform/log.h"
#include "platform/patch.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace rs7d3d {
void LogQuiet(const char* fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    try {
        Rs7Log("%s", buf);
    } catch (...) {
    }
}

void ModuleName(const void* a, char* out, size_t n) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = {};

    if (!a ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(a), &m) ||
        !GetModuleFileNameA(m, path, MAX_PATH)) {
        std::snprintf(out, n, "no module");
        return;
    }

    const char* base = path;

    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    std::snprintf(out, n, "%s", base);
}

const char* FamilyName(Rs7Family f) {
    return f == Rs7Family::Note ? "note" : f == Rs7Family::Additive ? "additive" : "embedded";
}

namespace {
uintptr_t SlotAddr(void* const* vt, unsigned slot) {
    return reinterpret_cast<uintptr_t>(&vt[slot]);
}
}  // namespace

bool VtSlotPair::Ours(void* const* vt) const {
    return reinterpret_cast<uintptr_t>(vt[lo]) == thunk.lo && reinterpret_cast<uintptr_t>(vt[lo + 1]) == thunk.hi;
}

bool VtSlotPair::Write(const void* device, void** vt, SlotValues) const {
    unsigned failed = 0;

    if (Rs7_WritePtrPair(SlotAddr(vt, lo), thunk.lo, thunk.hi, &failed)) {
        return true;
    }

    LogQuiet("%s: device %p vtable %p: cannot write slot %u - not hooked", tag, device, static_cast<const void*>(vt),
             lo + failed);

    return false;
}

void VtSlotPair::Restore(void** vt, SlotValues orig) const {
    uintptr_t old = 0;

    Rs7_WritePtrAtomic(SlotAddr(vt, lo + 1), orig.hi, &old);
    Rs7_WritePtrAtomic(SlotAddr(vt, lo), orig.lo, &old);
}

void VtSlotPair::Recheck(const void* device, void** vt, SlotValues orig, const Rs7MemReader& mem,
                         bool* foreignLogged) const {
    uintptr_t slots[2] = {};
    if (!mem.Read(SlotAddr(vt, lo), slots, sizeof slots)) {
        return;
    }

    if (slots[0] == thunk.lo && slots[1] == thunk.hi) {
        return;
    }

    if ((slots[0] == orig.lo || slots[0] == thunk.lo) && (slots[1] == orig.hi || slots[1] == thunk.hi)) {
        const bool ok = Write(device, vt, {slots[0], slots[1]});

        LogQuiet("%s: device %p: slots %u/%u were reset to d3d9's entry points - %s", tag, device, lo, lo + 1,
                 ok ? "hooked again" : "re-hook FAILED");
        return;
    }

    if (!*foreignLogged) {
        *foreignLogged = true;
        LogQuiet("%s: device %p: slot %u/%u now %08X/%08X (not ours, not d3d9's): left alone", tag, device, lo, lo + 1,
                 unsigned(slots[0]), unsigned(slots[1]));
    }
}
}  // namespace rs7d3d
