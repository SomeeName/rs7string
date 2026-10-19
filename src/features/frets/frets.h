#pragma once
#include <cstdint>
#include <string>

struct Rs7HookTable;
struct Rs7SetTable;
// The fret bars' hooks (group Core): F1, IN1 and the X_SETSCALE ref.
extern const Rs7HookTable kRs7FretHooks;

// Fret wires across the 7th string (docs/ARCHITECTURE.md, plan F1/F2).
//
// Each fret bar is one pool instance of MeshGuitarFretBarNormal (25 per guitar, FretGrid +5C40h,
// 0x38 each: +1Ch a world, +28h b world, +34h entity). Rnd_FretBars_ApplyTransforms 0x58FB20 only
// sets each bar's translation to the midpoint of its fretbar locators a/b; the bar length comes
// from the mesh (about +-4.6 around the midpoint), so the 7th string (one string spacing beyond
// string 0) lies outside it. For a guitar BUILT with 7 or 8 strings (C-lite: InstrumentInfo
// +0Bh, fixed for the guitar's life) every bar is moved towards string 6 and scaled uniformly so that
// the end at string 5 stays and the other end lies beyond string 6 by the wider of the two stock margins
// (L = [neck] fret_halflen, the bar's half length; d = |attach0 - attach1|). In normal order that is
// the string-0 margin: shift d/2, k = (L + d/2) / L. With inverted strings (InstrumentInfo +8) the
// attach points are mirrored while the stock bar stays centred at +0.27, so the string-0 margin is the
// narrow one (0.32 vs 0.86) and the bars grow by d + 0.54 (owner 2026-10-15: "the frets don't go all the
// way down"). The highway floor: the fret-wire bottom ends b (bar +28h world) are where the time axis
// (0x4B2C90), the neck fret numbers (0x4B1C00), the chord frames' origin edge, the stems of every note
// head and the highway surface are put. Stock puts b ~1.97 beyond the outer string. With inverted strings
// that is string 0's side, so the 7th lane lay 0.36 above the floor: fret numbers, 7th-string gems and the
// chord frames' lower edge sat on the 7th string. After the stock body of 0x58FB20 (which writes the world
// ends anew every call) b moves one string spacing further out when it lies on string 6's side; normal
// order (b beyond string 5) is untouched. Only the game's own setters
// are used (Entity_SetTranslation 0x7ADF70, Entity_SetScale 0x7AE2C0). The locators a/b and the span
// data are never written, so gems, highway, fret numbers and wrong-fret FX stay stock. Guitars
// built with 6 strings and bass are never touched.
//  - F1 (0x58FB20 entry): the stock body runs first; then, for a guitar built with 7/8 strings, the
//    bars are re-placed (the stock body has just put them back on the midpoint).
//  - F2 (in N5, per frame): applies when F1 could not (no attach points yet) and follows the live
//    overlay settings fret_extend / fret_halflen. Nothing is switched per song, tuner or menu.
// Logs once per run: the applied numbers and a read-back of the bar nodes 30 frames later.
// Logic: frets.cpp (Compute, FloorShift, ParseHalfLen, ChordFrame). Glue: hooks.cpp (F1 / F2, the floor, IN1, the
// chord-frame geometry, the per-guitar state), hooks_log.cpp (the log lines), hooks_wrap.cpp (F1, IN1, Wrap mid-hooks),
// registration.cpp (kRs7FretHooks).

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_fn_setscale;  // X_SETSCALE 0x7AE2C0 EAX = entity, cdecl(float)
__declspec(dllexport) extern uintptr_t g_rs7_frets_on;     // [neck] fret_extend (default 1)
__declspec(dllexport) extern float g_rs7_fret_halflen;     // [neck] fret_halflen (default 4.61)
// F1, after the stock body of 0x58FB20 (cdecl, inside pushad). grid = the FretGrid (stock EAX).
// Returns at once (no Win32 call) unless the grid belongs to a guitar built with 7 or 8 strings.
__declspec(dllexport) void __cdecl Rs7_FretsAfterApply(uint8_t* grid);
// IN1, after Rnd_Inlays_ApplyTransforms 0x59BA80 (which puts the inlay entity at the frame entity's translation;
// the dots are authored centred on strings 0-5): a guitar built with 7/8 strings gets the entity at that
// translation + (attach0 - attach1) / 2, the centre of 7 strings, in either string order. Part of fret_extend.
__declspec(dllexport) void __cdecl Rs7_InlaysAfter(uint8_t* guitar, uint8_t* inlays);
}

namespace rs7frets {
constexpr float kDefaultHalfLen = 4.61f;   // half bar length estimate (bound radius 4.638, see the note)
constexpr uint32_t kGuitarStrings = 0x0C;  // Rnd_Guitar: GuitarStrings*
constexpr uint32_t kGuitarGrid = 0x10;     // Rnd_Guitar: FretGrid* (0x593043: EAX = [edi+10h])
constexpr uint32_t kGuitarInfo = 0x24;     // Rnd_Guitar: info (+0Dh fret bar count, 25)
constexpr uint32_t kBars = 0x5C40;         // FretGrid: fretBar[26] x 0x38
constexpr uint32_t kBarStride = 0x38;
constexpr uint32_t kBarALocal = 0x04;  // the fretbar locators as read (0x58F7F0)
constexpr uint32_t kBarBLocal = 0x10;
constexpr uint32_t kBarAWorld = 0x1C;
constexpr uint32_t kBarBWorld = 0x28;
constexpr uint32_t kBarEntity = 0x34;
constexpr int kMaxBars = 26;
constexpr int kMaxGuitars = 8;
constexpr int kVerifyFrames = 30;  // N5 frames after an apply before the node read-back

struct Extend {
    float shift[3];  // added to each bar's stock midpoint: (attach0 - attach1) / 2
    float scale;     // uniform scale k = (L + d/2) / L
    float spacing;   // d = |attach0 - attach1|
};

// Pure. a0/a1 = string 0/1 attach points (local; only their difference is used, so lefty and
// inverted layouts are handled by the attach points themselves). False (out untouched) for a
// degenerate spacing or half length.
bool Compute(const float a0[3], const float a1[3], const float mid[3], float halfLen, Extend* out);
// [neck] fret_halflen text -> value: a finite number in [1, 20] (blanks around it allowed).
bool ParseHalfLen(const wchar_t* text, float* out);
// The highway floor: a0 / a1 = string 0 / 1 attach points, b = a fret wire's bottom end (all guitar-local). b beyond
// string 0 on string 6's side -> true and out = a0 - a1 (one string spacing further out); else false.
bool FloorShift(const float w0[3], const float w1[3], const float b[3], float out[3]);

// A chord frame is one model whose origin edge sits on the fret wires' bottom end b (the highway floor, moved by
// FloorShift on 7/8-string guitars) and which extends towards the lanes by H = 5d + 2m (d = string spacing, m = the
// stock margin beyond the outer string of lanes 0-5; HYPOTHESIS: the model is symmetric about lanes 0-5, which is what
// the verified 1.134 = (6d + 2m) / (5d + 2m) assumed). ChordFrame derives the span from the lane positions: the
// frame must cover lanes 0-5 (stock: 6-string chords) or 0-`top` (`top` 6/7 = the highest string the chord uses), each
// with margin m. The origin edge STAYS on the floor (owner 2026-10-15: a frame translated by the floor shift floated
// above the fret numbers); only the far edge moves: along the string axis in model units scale `scale` (on the
// stock extent H), and `shift` (0 except where the origin edge lies inside the outermost lane + margin: a floor that
// was not extended, the 8th lane) moves the origin edge out, in the direction away from the lanes.
// a0 / a1 = string 0 / 1 attach points and b = fret bar 0's bottom locator (all guitar-local, never written);
// floorMoved = FloorShift has moved the world floor by one spacing (b beyond string 6 only). False for a degenerate
// geometry (out untouched).
struct FrameFit {
    float scale;
    float shift;
    float origin;   // diagnostics: the origin edge as placed now, in p (0 at string 0, string i at -i d, + towards
                    // string 6)
    float margin;   // m
    float spacing;  // d
    float bLocal[3], bWorld[3], a0[3];  // diagnostics, set by Rs7Frets_ChordFrame: bar 0 locator B (local / the world
                                        // end as placed now), string 0 attach
};

bool ChordFrame(const float a0[3], const float a1[3], const float b[3], bool floorMoved, int top, FrameFit* out);
}  // namespace rs7frets

// B2: ChordFrame for the guitar `guitar` (Rnd_Guitar*): reads its attach points and bar 0, and whether F1 moved its
// floor. False when the guitar's strings / bars are not there or the geometry is degenerate.
bool Rs7Frets_ChordFrame(const uint8_t* guitar, int top, rs7frets::FrameFit* out);

// F2, from N5 (Rs7_S7Update) every frame for its guitar: want = the guitar was built with 7/8 strings.
// fret_extend / fret_halflen are live (overlay, platform/settings.h): with g_rs7_frets_on = 0 an applied
// guitar is restored at its next frame and nothing else is touched; a new g_rs7_fret_halflen is
// applied again at the next frame.
void Rs7Frets_Frame(uint8_t* guitar, bool want);
// The guitar is being destroyed (its GuitarStrings are freed): drop its state slot.
void Rs7Frets_Forget(const uint8_t* guitar);
void Rs7Frets_ResetLog();

// Overlay status: guitars with frets state, guitars whose bars are applied now.
struct Rs7FretsStats {
    int guitars, applied;
};

void Rs7Frets_Stats(Rs7FretsStats* out);  // song setup: the frets log lines may appear again

// ---- settings (settings.cpp)

// [neck] fret_extend, fret_halflen.
extern const Rs7SetTable kRs7FretSettings;
// The fret extend status row and its Neck section (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7FretUi;

// [neck] as read at startup.
//   fret_extend=0|1          1 (default): on 7-string songs the 25 fret bars are moved half a string
//                            spacing towards the 7th string and scaled so they span it (F1/F2).
//                            0: F1 replays stock, the bars are never touched.
//   fret_halflen=4.61        the bar's half length L used for the scale k = (L + d/2) / L, 1..20.
//                            Invalid text or out of range: the default, logged.
struct Rs7FretConfig {
    int fretExtendRaw = 1;
    bool fretExtend = true;        // raw != 0
    std::wstring fretHalfLenRaw;   // the text as read (for the log)
    bool fretHalfLenValid = true;  // missing, empty or a number in [1, 20]
    float fretHalfLen = 4.61f;
};

// Reads [neck] fret_extend / fret_halflen from a resolved ini path (nullptr: the defaults).
Rs7FretConfig Rs7Frets_ReadIni(const wchar_t* iniPath);
// The last startup read (Rs7Set_ReadIni).
const Rs7FretConfig& Rs7Frets_Config();
