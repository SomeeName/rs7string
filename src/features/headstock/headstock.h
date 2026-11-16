#pragma once
#include <cstdint>

// The extended headstock (docs/ARCHITECTURE.md section 5). The public header: the only one code outside
// src/features/headstock/ includes.
//
// Two parts, one feature:
//  - The tuning pegs as a native 8-slot PegSet (pegset.h): pegs 7 and 8 are more stock slots, peg 7 loaded for
//    every 6-string guitar, the extra pegs active on a guitar built with 7 or 8 strings (PS* sites; pegset.cpp
//    logic, hooks_pegset.cpp glue, hooks_mid.cpp / hooks_stubs.cpp the sites). [debug] head_cull (hooks_debug.cpp).
//  - The 7-string headstock model (assetload.h): the stock frame's nif carries our in-line model as a culled rs7_alt
//    child, shown per frame on a guitar that shows the 7th string (AL1, PGH1, LT1; assetload_*.cpp). The model is
//    built in the DLL from the user's own stock assets (hsbuild.cpp logic, hsbuild_io.cpp / headstockread.cpp glue).
//    [assets] headstock7, headstock7_file, dir, suffix, selfbuild, headstock7_wood.
//
// registration.cpp holds the two hook tables: the PS* sites (group Pegs) and the shared ones with the refs (group
// Core). state.h holds the PegSet glue's state. settings.cpp holds the [debug] head_cull and [assets]
// keys. Native tests: tests/headstock/.

struct Rs7HookTable;
struct Rs7SetTable;

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_head_on;  // 1: peg 7 loaded (0 only in the emulator tests)
// The native PegSet (a member of the native 7-string unit). Set by Rs7_Startup before any site is patched; cleared
// only by a failing startup check (then nothing is patched).
__declspec(dllexport) extern uintptr_t g_rs7_native_pegset;
__declspec(dllexport) extern uintptr_t g_rs7_asset_on;  // [assets] headstock7 (default 1)
// LT1 (UI_PlayerTuner_PlacePegLetters): the in-line byte (+0Ah) the letter rule uses for InstrumentInfo `info`:
// 1 for a guitar that shows the 7-string in-line headstock on a non-in-line (3+3) frame, else info's own byte.
__declspec(dllexport) int __cdecl Rs7Asset_LetterInline(const uint8_t* info);
}

// The PS* sites (group Pegs) and the shared headstock hooks with their refs (group Core), registration.cpp.
extern const Rs7HookTable kRs7HeadstockHooks;
extern const Rs7HookTable kRs7HeadstockCoreHooks;

namespace rs7head {
constexpr float kRaiseFadeIn = 0.75f;  // the tuner's / guitar's stock fade-in duration (0x5AEB50)
}

// ---- settings (settings.cpp)

// [debug] head_cull; [assets] headstock7, headstock7_file, headstock7_file_lh,
// headstock8_file, headstock8_file_lh, headstock7_wood, dir, suffix.
extern const Rs7SetTable kRs7HeadstockSettings;
// The headstock's player settings, the extra tuning pegs status row and its Neck section (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7HeadstockUi;

// ---- startup (hooks.cpp)

// Applies the native PegSet switch and the [assets] / [debug] head_cull keys from the loaded rs7string.ini and logs
// them. Before any site is patched. A 6-string guitar's native PegSet always loads 7 pegs (PSL1, g_rs7_head_on).
void Rs7Headstock_Configure(const wchar_t* iniPath);

// ---- per frame, from N5 (Rs7_S7Update), game thread

// The pegs: want = the guitar was built with 7/8 strings.
void Rs7Head_Frame(uint8_t* guitar, bool want);
// Every N5 frame of a native guitar (game thread): with a [neck] spacing_* factor for this extended instrument
// (features/spacing) the headstock frame is narrowed across the strings by it and the pegs follow (hooks_width.cpp).
void Rs7Head_Width(uint8_t* guitar, bool extended);
void Rs7Head_ForgetWidth(const uint8_t* guitar);  // destruction: forget without dereferencing the old frame
// The model: want = the guitar shows the 7th string this frame.
void Rs7Asset_HeadFrame(uint8_t* guitar, bool want);

// ---- overlay, log, other features

// Overlay (platform/settings.h, any thread): [debug] head_cull. The next N5 frame of a 7-string guitar takes it; from
// the first call on, the ini is no longer re-read.
void Rs7Head_SetLive(const char* cull);

// Overlay status: native PegSets constructed, loads with 7 or 8 pegs, raises of the extra pegs (counted since startup).
struct Rs7HeadStats {
    int pegsets, loaded7, active7;
    bool liveOwned;
};

void Rs7Head_Stats(Rs7HeadStats* out);
void Rs7Head_ResetLog();
// The guitar whose strings object is `gs` is released: its per-guitar model state slot is freed (leak audit).
void Rs7Asset_ForgetStrings(const uint8_t* gs);
// The same by the guitar's address (no read of the guitar object: it may already be freed).
void Rs7Asset_ForgetGuitar(const uint8_t* guitar);
int Rs7Asset_LiveHeads();
// The colour param (g_diffuseColor) of string_size_<string + 1> (string 6 or 7) while this guitar shows the edited
// model: the segment the native headstock colour loop (features/strings/strings.h, GSHC) binds to that string.
// nullptr for any other string, a model without that segment, or the stock headstock.
uint8_t* Rs7Asset_SegColour(uint8_t* guitar, uint32_t string);
