#pragma once
#include "hooks/midhook.h"
#include <cstdint>

// String 6's own colour (Step 1/2), read by the native colour matrix (core/colournative.h): V per colour mode,
// 0 = the stock preview row colorUV[mode][17][0] (set by the ramp swap, Step 2), and RGB [mode*3 + channel] from
// [color] string6_color / string6_color_cb, set by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) float g_rs7_v6[2];
extern "C" __declspec(dllexport) float g_rs7_fx_rgb[6];
// rs7string.ini [debug] diagnostics: 0 = R2/R1 skip their D1/D4 logger calls (and Rs7_Startup does
// not write D2/D3/D5), 1 = log. Set by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) uintptr_t g_rs7_diag;
// Data references (ref sites, never written), bound by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) uintptr_t g_rs7_colormgr;      // &g_pRnd_ColorManager (X_CM)
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_cs;    // CRITICAL_SECTION* of the NiTexture list (X_TCS)
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_head;  // &list head (X_THEAD)

struct Rs7Hook {
    const char* id;
    void* stub;         // nullptr for ref sites
    uintptr_t* resume;  // site + length, for stubs that jump back (nullptr if unused)
    uintptr_t* target;  // original branch/call target, or a ref site's dword (nullptr if unused)
    bool diagOnly;      // diagnostics only (D2/D3/D5): resolved always, written only with diagnostics=1
    bool ref;           // data reference (Rs7Kind::Ref): resolved always, never written
    bool vgOnly =
        false;     // virtual guitarist (TICK/JUD1/JUD2/AUD1): resolved always, written only with [test] vguitar=1
    int pegs = 0;  // kRs7PegNative: written only with the native PegSet
    int strs = 0;  // kRs7StrNative: written only with the native GuitarStrings
    Rs7Mid::Callback callback = nullptr;  // generic mid-hook (Jmp only); stub unused. resume / target are still bound
                                          // (site + length / the branch target) for callbacks that assign ctx.jump
    int fg = 0;                           // kRs7FgNative: written only with [neck] native_fretgrid=1 (core/fretgrid.h)
    int pitch = 0;                        // kRs7PitchNative: written only with [neck] native_pitch=1
    int ng = 0;                           // kRs7NgNative: written only with the native next-note grid (core/nextgrid.h)
    bool publish = false;                 // verified preparation ports, request + all five native prerequisites
    int tuner = 0;                        // kRs7TunerNative: written only with [neck] native_tuner=1 (core/tuner8.h)
};

constexpr int kRs7PegNative = 1;  // the native N-slot PegSet (core/headstock.h, issue #18 stage 3)
constexpr int kRs7StrNative = 1;  // the native N-slot GuitarStrings (core/gsnative.h, issue #18 stage 4)
constexpr int kRs7FgNative = 1;   // the native capacity-8 FretGrid (core/fretgrid.h, issue #18 stage 5 area 1)
constexpr int kRs7PitchNative =
    1;  // native 8-entry tuning / open-MIDI storage (core/pitch8.h, issue #18 stage 5 area 3)

constexpr int kRs7NgNative = 1;     // the native capacity-8 next-note grid (core/nextgrid.h, issue #18 stage 5 area 2)
constexpr int kRs7TunerNative = 1;  // the native 8-slot tuner (core/tuner8.h, issue #18 job 3)

const Rs7Hook* Rs7_FindHook(const char* id);

// Feature groups for failure isolation. A site that cannot be resolved / prepared / written disables only
// its own group (Rs7_DisableFeature); Core (the base 7-string sites, X_ refs and everything unlisted)
// cannot be isolated: a Core failure still aborts patching.
enum class Rs7Feature {
    Core,
    Colours,
    Pegs,
    Strings,
    Fretgrid,
    Pitch,
    Nextgrid,
    Diagrams,
    Vguitar,
    Diag,
    Publish,
    Chords,
    Tuner,
    Psarc,
    Clite,
    Count
};
Rs7Feature Rs7_FeatureOf(const Rs7Hook& h);
const char* Rs7_FeatureName(Rs7Feature f);
// Marks the group failed: Rs7_ShouldPatch is false for all its sites from now on and the group's runtime
// switch is cleared (native colours -> stock six-column manager with the legacy C1-C5 colour sites).
// Returns false (and changes nothing) for Core. Idempotent.
bool Rs7_DisableFeature(Rs7Feature f);
// The native 7-string unit (Colours, Pegs, Strings, Fretgrid, Pitch, Nextgrid, Publish, Tuner, Clite): since 1.0 there
// is no legacy fallback for any of them, so at startup a member that fails (resolve, preparation, writing, or a
// startup check) is handled like a core failure: nothing is patched and the game stays stock (six strings). A loss at
// runtime (Rs7_DisableFeature from a deferred FretGrid write) still only clears the switches the stubs read.
bool Rs7_IsSevenUnit(Rs7Feature f);
bool Rs7_FeatureDisabled(Rs7Feature f);
void Rs7_ResetFeatureFailures();  // tests
// Whether Rs7_Startup writes the hook's site (every site is resolved either way; ref sites never).
bool Rs7_ShouldPatch(const Rs7Hook& h, bool diagnostics, bool vguitar = false, bool nativePegs = false,
                     bool nativeStrings = false);
