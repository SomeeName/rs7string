#pragma once
#include <cstdint>

// The per-guitar 7-string state behind N5 (Rnd_Guitar_Update, every frame) - see string7.cpp. String 6 itself is a
// stock object of the native GuitarStrings (core/gsnative.h); the stage-1 DLL-owned 7th string object S7 / S7G
// (N2-N4, N6, N7, N10, N11, N14-N17) was removed in 1.0.
//  - A guitar built with 7/8 strings (C-lite) is driven by its build count alone (NativeFrame).
//  - Otherwise (no native publication at runtime) the per-song rule applies: S4 marks a guitar 7-string at song
//    setup and arms it; Rs7S7_Visibility decides per frame whether it shows the 7-string look (frets, headstock
//    model, 7th peg, the native string 6's active count).
//  - N12/N13 put string 6 into the SNG Section.stringMask (Rs7_S7MaskBackfill), so the stock section dimming
//    covers it; N18-N23 and PL1 reach the native string 6 through Rs7_S7BendTarget / Rs7_S7Pluck.

// Game functions and data, bound by Rs7_Startup from callref/ref sites (never written); the native GuitarStrings,
// pegs, frets and headstock model use them.
extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_fn_alloc;    // X_ALLOC   0x8F9D40 cdecl(size, 0)
__declspec(dllexport) extern uintptr_t g_rs7_fn_strctor;  // X_STRCTOR 0x598220 ESI=this, EAX=kind
__declspec(dllexport) extern uintptr_t g_rs7_fn_shape;    // X_SHAPE   0x5996A0 stdcall(obj, info, s)
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_detach;  // X_DETACH  0x7BF1D0 cdecl(entity, detach, flag): Entity_SetSceneDetached
__declspec(dllexport) extern uintptr_t g_rs7_fn_show;       // X_SHOW    0x7AE370 EAX=entity, cdecl(show)
__declspec(dllexport) extern uintptr_t g_rs7_fn_setpos;     // X_SETPOS  0x7ADF70 cdecl(entity, &vec3)
__declspec(dllexport) extern uintptr_t g_rs7_fn_setrot;     // X_SETROT  0x7AE1A0 cdecl(entity, &euler)
__declspec(dllexport) extern uintptr_t g_rs7_frame_global;  // X_DT      0x135FBFC (dt float at [[it]+0C0h])
__declspec(dllexport) extern uintptr_t g_rs7_s7_on;         // [neck] string7_line (read by N1, N12 and C)
// Some player in 7-string mode at the last song setup (S4). Log/diagnostics only.
__declspec(dllexport) extern volatile long g_rs7_any7;
// [neck] string7_v: the 7th string's atlas row (v_positionString / g_VPosition), used by the native strings
// (gsnative.h). Default 0.4 = string 0's row (low E); the bass rows 0.0..0.3 are filled too.
__declspec(dllexport) extern float g_rs7_s7_v;

// N5 (cdecl, inside pushad). None throws.
__declspec(dllexport) void __cdecl Rs7_S7Update(uint8_t* guitar);
// The native GuitarStrings register / drop their per-guitar entry (gsnative.cpp); Rs7_S7Free also forgets the
// guitar's fret, headstock-model and S4 state.
__declspec(dllexport) void __cdecl Rs7_S7Record(void* gs);
__declspec(dllexport) void __cdecl Rs7_S7Free(void* gs);
// PL1 (chord string 6): plucks the native string 6 (the native copy of the stock pluck). 1 = done.
__declspec(dllexport) int __cdecl Rs7_S7Pluck(void* gs, uint32_t sngFret, uint32_t fret);
// N13 (integer-only, every level of every SNG load): the stock back-fill for bit 6 of
// Section.stringMask[level] (song = the SNG song: sections at +0F4h/+0F8h, 0x58 bytes each, mask
// bytes at +34h + level). A no-op without string-6 notes.
__declspec(dllexport) void __cdecl Rs7_S7MaskBackfill(uint8_t* song, uint32_t level);
// The native string 6 of this GuitarStrings, else nullptr.
__declspec(dllexport) uint8_t* __cdecl Rs7_S7DimTarget(void* gs);
// N18-N20, N22/N23 (no x87 use): the native string object the stock string-bend calls of 0x7E3700 / 0x7E4230 act
// on for string 6, else nullptr. Kind 0 (N18: Rnd_GuitarString_SetBend 0x598910 from the detected bend) and 1
// (N19/N20: release): string 6; kind 2 (N22: the chart bend, "ghost_up_NN") and 3 (N23: its release): its ghost.
// Logs the first bend of each kind per song.
__declspec(dllexport) uint8_t* __cdecl Rs7_S7BendTarget(void* gs, uint32_t kind, uint32_t fret, uint32_t amountBits);
}

namespace rs7s7 {
constexpr float kDefaultAtlasRow = 0.4f;       // [neck] string7_v default: string 0's row (low E)
constexpr float kDefaultDim = 76.0f / 255.0f;  // 0x4C: the stock dimmed value (state 2), see Rs7S7_DimColour
constexpr int kMaxGuitars = 8;                 // side table entries (GuitarStrings*)
}  // namespace rs7s7

// fix/escrebuild: the guitar object `guitar` is gone (released by the game, e.g. a clite rebuild): every per-guitar
// state keyed by its address is dropped WITHOUT touching the object (assetload headstock slot, fret bar slot, the
// song/armed mode). Idempotent; null is ignored. Game thread.
void Rs7_GuitarGone(void* guitar);
void Rs7S7_ResetLog();  // song setup: the string7 log lines may appear again
// S4 (song setup of a session): whether `guitar` (session+37Ch, Rnd_Guitar*) plays a 7-string
// arrangement. A 7-string mark is also "armed": N5 shows the 7-string look from this moment (song load), before
// the stock intro fade-in (session slot 3 0x7DFF50 / slot 2 0x7DFB50 -> 0x592EC0 -> FadeIn 0x59A820). The arm ends
// at the song end (string 0's +70h falls) or when the stock strings fade out before the song started
// (GuitarStrings +38h rises: quit during the intro); after that the look shows only while the song runs (+70h,
// e.g. a restart that does not run S4 again). The mark itself lasts until the next S4 of the same guitar.
void Rs7S7_SetGuitarMode(void* guitar, bool seven);
// The guitar's native string 6 (Rs7_S7DimTarget of its GuitarStrings), else nullptr.
uint8_t* Rs7S7_ObjectOf(void* guitar);

namespace rs7s7 {
// Issue #27: how long after +38h rose the arm ends (tests: 0).
extern uint32_t g_fadeHoldMs;
// Why an armed S4 mark was dropped this frame (N5).
enum class Disarm { None, FadedOut, Unseen };

struct Visibility {
    bool want;        // the 7-string look shown this frame
    Disarm disarmed;  // the arm ended this frame (logged once per song)
};

// N5's visibility rule (pure; `armed` is the guitar's arm, updated in place):
//   seven            S4 marked this guitar 7-string (the mark of the last S4)
//   song             stock string 0's +70h (song running: 0x7DFB50 .. 0x7E0040)
//   songEnded        +70h fell this frame
//   fadedOut         GuitarStrings +38h ("faded out", set by FadeOut 0x59A720) this frame
//   fadeOutDone      +38h rose (FadeOut started) and g_fadeHoldMs passed since
//   unseen           N5 did not run for this guitar for kUnseenMs before this frame (its scene was
//                    gone, or the guitar object is reused)
// want = seven && (armed || song). Issue #27: the arm outlives the song (the outro still shows the
// guitar) and ends, while no song runs, once the stock strings have faded out (FadedOut: the fade
// finished, or +38h was already set when the song ended - quit from a faded pause), or when the
// guitar was unseen (Unseen). Until then the frets / headstock / pegs / string 6 stay 7-string. The tuner, menus and
// 6-string/bass songs: no mark (or an ended arm) and no +70h.
Visibility Rs7S7_Visibility(bool seven, bool& armed, bool song, bool songEnded, bool fadedOut, bool fadeOutDone,
                            bool unseen);
}  // namespace rs7s7

// The stock relation between a string's state 1 and state 2 colour (GameColorManager data, all 12
// strings of both colour modes): same hue and saturation, value (max channel) = dimValue.
// rgb black stays black.
void Rs7S7_DimColour(const float rgb[3], float dimValue, float out[3]);
// String 6's glow colour (state 3, colorPtr[mode][3][6]). The stock state-3 colours are the state-1 hues at full
// value (max channel 0xFF) and full saturation (FF4F5A -> FF0010, 1DACF9 -> 00A9FF; snapshot palette). A grey has
// no hue to saturate, so this keeps the saturation and only scales the [color] string6 colour to max channel 1
// (C8C8C8 -> white). Black stays black.
void Rs7S7_GlowColour(const float rgb[3], float out[3]);
// The back-fill of SNG PostProcessNotes 0x55CD30..0x55CE3D for one string bit and one level over
// `count` sections (stride 0x58, mask byte at +34h + level): walking down from the last section, a
// section that uses the string lights it in the section before (one step only). Returns the number
// of sections that use the string after the back-fill; *added = the sections it lit early.
int Rs7S7_BackfillBit(uint8_t* sections, int count, uint32_t level, uint8_t bit, int* added);

// Overlay status: GuitarStrings registered, guitars marked 7-string by S4.
struct Rs7S7Stats {
    int guitars, sevenGuitars;
};

void Rs7S7_Stats(Rs7S7Stats* out);
