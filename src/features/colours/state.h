#pragma once
#include "features/colours/colours.h"
#include <windows.h>

// Everything the colour feature keeps between calls. The switches and the palette other code reads by name
// (g_rs7_native_colours, g_rs7_glow_fix; g_rs7_v6 / g_rs7_fx_rgb in native.cpp) stay named exports: the
// emulator tests and the stubs bind them by symbol.
namespace rs7colour {
// The eighth string's placeholder colour per mode until its own colour exists: teal / colourblind orange.
constexpr uint32_t kPlaceholder[2] = {0x00D6B2, 0xFF8C00};

// A raw dword store into the manager (UV floats and indices are copied as bits).
inline void PutBits(uint8_t* mgr, uint32_t off, uint32_t bits) {
    *reinterpret_cast<uint32_t*>(mgr + off) = bits;
}

struct State {
    // The native matrix (native.cpp): only extra RGB values are owned here. Stock source-list pointers are never
    // copied, replaced or freed. Static storage survives teardown, rebuild and mode switches.
    float rgb[2][28][2][3] = {};
    uint32_t fallback7 = 0xC8C8C8;  // CMFALL: string 6's colour as 0xRRGGBB, set by Publish

    // Publication (hooks_publish.cpp, CMINIT / CMBUILD): see the rules there.
    void* volatile built = nullptr;
    volatile LONG dirty = 0;
    volatile DWORD cmTid = 0;  // thread that ran the last CMBUILD (0: none yet)
    LONG loggedFault = 0, loggedForeign = 0;
    void* volatile loggedInit = nullptr;
    void* volatile loggedBuild = nullptr;

    // RSMods (rsmods.cpp): what Init found.
    rs7rsmods::Colour rsmods;
    bool rsmodsPresent = false;
    bool customGlow = false;

    // The corrected glow colours Rs7_GlowFixPtr hands out (render thread).
    float glow[2][6][3] = {};

    // The ramp candidate table (rampstate.cpp).
    Rs7Cand cands[kRs7MaxCands] = {};
    volatile LONG candCount = 0;
    SRWLOCK addLock = SRWLOCK_INIT;  // serialises writers of the table (Add, Claim, Retire, ResetForTests)
    SRWLOCK v6Lock = SRWLOCK_INIT;   // serialises UpdateV6 (rule + store + log as one step)
    volatile LONG swaps = 0, hidden = 0, faults = 0;
};

extern State g_colour;
}  // namespace rs7colour
