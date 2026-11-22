#pragma once
#include "features/colours/colours.h"
#include "platform/freeze.h"
#include <windows.h>

// Everything the colour feature keeps between calls. The switches and the palette other code reads by name
// (g_rsxr_native_colours, g_rsxr_glow_fix; g_rsxr_v6 / g_rsxr_fx_rgb in native.cpp) stay named exports: the
// emulator tests and the stubs bind them by symbol.
namespace rsxrcolour {
// A raw dword store into the manager (UV floats and indices are copied as bits).
inline void PutBits(uint8_t* mgr, uint32_t off, uint32_t bits) {
    *reinterpret_cast<uint32_t*>(mgr + off) = bits;
}

struct State {
    // Startup writes plain storage under the freeze and logs it only after every thread resumes.
    const char* patchReason = "none";
    const char* patchRange = "none";
    RsxrFrozenRefusal patchRefusal{};
    SRWLOCK paletteLock = SRWLOCK_INIT;
    uint32_t pendingPalette[2] = {kRsxrDefaultColour, kRsxrDefaultColour};
    bool palettePending = false;
    // The native matrix (native.cpp): only extra RGB values are owned here. Stock source-list pointers are never
    // copied, replaced or freed. Static storage survives teardown, rebuild and mode switches.
    float rgb[2][kStates][2][3] = {};
    uint32_t fallback7[2] = {kRsxrDefaultColour,
                             kRsxrDefaultColour};  // CMFALL: string 6's colour per mode as 0xRRGGBB, set by SetPalette
    // An extended-bass song (SetBassLowB): colour slot 4 (the bass's low B) shows string 6's colours. The stock slot-4
    // pointers and UV the swap replaced, and the values it wrote: the next Publish restores an entry only while it
    // still holds what the swap wrote (the game's table build in between rewrites slot 4, hooks_publish.cpp).
    volatile LONG bassLowB = 0;
    bool bassSwapped = false;
    uint8_t* swappedMgr =
        nullptr;  // the ColourManager whose slot 4 is swapped (a new manager has its own stock slot 4)
    float* savedPtr4[2][kStates] = {};
    uint32_t savedUV4[2][kStates] = {};
    float* swappedPtr4[2][kStates] = {};
    uint32_t swappedUV4[2][kStates] = {};
    // The eighth string's colour per mode as 0xRRGGBB ([color] string7_color / _cb, SetPalette8).
    volatile uint32_t rgb8[2] = {kRsxrDefaultColour7, kRsxrDefaultColour7};

    // Publication (hooks_publish.cpp, CMINIT / CMBUILD): see the rules there.
    void* volatile built = nullptr;
    volatile LONG dirty = 0;
    volatile DWORD cmTid = 0;  // manager thread established by CMINIT / CMBUILD (0: none yet)
    LONG loggedFault = 0, loggedForeign = 0;
    unsigned publishFailures = 0;  // manager thread: quarantine after three consecutive faults, until CMBUILD
    void* volatile loggedInit = nullptr;
    void* volatile loggedBuild = nullptr;

    // RSMods (rsmods.cpp): what Init found.
    rsxrrsmods::Colour rsmods;
    bool rsmodsPresent = false;
    bool customGlow = false;

    // The corrected glow colours Rsxr_GlowFixPtr hands out (render thread).
    float glow[2][6][3] = {};

    // The ramp candidate table (rampstate.cpp).
    RsxrCand cands[kRsxrMaxCands] = {};
    volatile LONG candCount = 0;
    SRWLOCK addLock = SRWLOCK_INIT;  // serialises writers of the table (Add, Claim, Retire, ResetForTests)
    SRWLOCK v6Lock = SRWLOCK_INIT;   // serialises UpdateV6 (rule + store + log as one step)
    volatile LONG swaps = 0, hidden = 0, faults = 0;
};

extern State g_colour;
void ApplyPendingPalette();  // manager thread, or startup before CMINIT establishes it; releases lock before game reads
}  // namespace rsxrcolour
