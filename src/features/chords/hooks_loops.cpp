// The string objects of the CL* mid-hooks (hooks_cl.cpp): the native GuitarStrings main or ghost string 6/7.
#include "features/chords/chords.h"
#include "features/strings/strings.h"
#include "platform/safecopy.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_chordloops = 0;

extern "C" uint8_t* __cdecl Rs7ChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost) {
    if (s < 6 || s >= 8 || !g_rs7_native_strings || !rs7gs::IsNative(owner)) {
        return nullptr;
    }

    // Guarded reads: a mid-hook has no SEH frame, so a stale info pointer must read as "not native", not crash.
    const uint8_t* info = nullptr;
    uint32_t magic = 0;
    uint8_t loaded = 0, active = 0;

    if (!Rs7_SafeCopy(&info, owner + rs7gs::kInfo, sizeof info) || !info ||
        !Rs7_SafeCopy(&magic, info + rs7gs::kInfoMagicOff, sizeof magic) || magic != rs7gs::kInfoMagic ||
        !Rs7_SafeCopy(&loaded, info + rs7gs::kInfoLoaded, 1) || !Rs7_SafeCopy(&active, info + rs7gs::kInfoActive, 1) ||
        loaded > 8 || active > loaded || s >= loaded) {
        return nullptr;
    }

    return Rs7Gs_Active(owner, static_cast<int>(s), ghost != 0);
}
