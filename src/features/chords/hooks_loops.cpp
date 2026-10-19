// The string objects of the CL* mid-hooks (hooks_cl.cpp): the native GuitarStrings main or ghost string 6/7.
#include "features/chords/chords.h"
#include "features/strings/strings.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_chordloops = 0;

extern "C" uint8_t* __cdecl Rs7ChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost) {
    if (s < 6 || s >= 8 || !g_rs7_native_strings || !rs7gs::IsNative(owner)) {
        return nullptr;
    }

    const auto* info = *reinterpret_cast<uint8_t* const*>(owner + rs7gs::kInfo);
    if (!info || *reinterpret_cast<const uint32_t*>(info + rs7gs::kInfoMagicOff) != rs7gs::kInfoMagic ||
        info[rs7gs::kInfoLoaded] > 8 || info[rs7gs::kInfoActive] > info[rs7gs::kInfoLoaded] ||
        s >= info[rs7gs::kInfoLoaded]) {
        return nullptr;
    }

    return Rs7Gs_Active(owner, static_cast<int>(s), ghost != 0);
}
