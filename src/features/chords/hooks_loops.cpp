// The string objects of the CL* mid-hooks (hooks_cl.cpp): the native GuitarStrings main or ghost string 6/7.
#include "features/chords/chords.h"
#include "features/strings/strings.h"
#include "platform/safecopy.h"

extern "C" __declspec(dllexport) uintptr_t g_rsxr_native_chordloops = 0;

extern "C" uint8_t* __cdecl RsxrChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost) {
    if (s < 6 || s >= 8 || !g_rsxr_native_strings || !rsxrgs::IsNative(owner)) {
        return nullptr;
    }

    // Guarded reads: a mid-hook has no SEH frame, so a stale info pointer must read as "not native", not crash.
    const uint8_t* info = nullptr;
    uint32_t magic = 0;
    uint8_t loaded = 0, active = 0;

    if (!Rsxr_SafeCopy(&info, owner + rsxrgs::kInfo, sizeof info) || !info ||
        !Rsxr_SafeCopy(&magic, info + rsxrgs::kInfoMagicOff, sizeof magic) || magic != rsxrgs::kInfoMagic ||
        !Rsxr_SafeCopy(&loaded, info + rsxrgs::kInfoLoaded, 1) ||
        !Rsxr_SafeCopy(&active, info + rsxrgs::kInfoActive, 1) || loaded > 8 || active > loaded || s >= loaded) {
        return nullptr;
    }

    return RsxrGs_Active(owner, static_cast<int>(s), ghost != 0);
}
