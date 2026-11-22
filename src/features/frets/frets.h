#pragma once
#include <cstdint>
#include <string>

#include "model/instrument.h"

struct RsxrHookTable;
struct RsxrSetTable;
// The fret bars' hooks (group Core): F1, IN1 and the X_SETSCALE ref.
extern const RsxrHookTable kRsxrFretHooks;

// Fret wires across the extra strings (docs/ARCHITECTURE.md).
//
// Each fret bar is one pool instance of MeshGuitarFretBarNormal (25 per guitar, FretGrid +5C40h,
// 0x38 each: +1Ch a world, +28h b world, +34h entity). Rnd_FretBars_ApplyTransforms 0x58FB20 only
// sets each bar's translation to the midpoint of its fretbar locators a/b; the bar length comes
// from the mesh (about +-4.6 around the midpoint), so the 7th string (one string spacing beyond
// string 0) lies outside it. For a guitar BUILT with 7 or 8 strings (C-lite: InstrumentInfo
// +0Bh, fixed for the guitar's life) every bar is moved towards string 6 and scaled uniformly so that
// the end at string 5 stays and the other end lies beyond string 6 by the wider of the two stock margins
// (L = kDefaultHalfLen, the bar's half length; d = |attach0 - attach1|). In normal order that is
// the string-0 margin: shift d/2, k = (L + d/2) / L. With inverted strings (InstrumentInfo +8) the
// attach points are mirrored while the stock bar stays centred at +0.27, so the string-0 margin is the
// narrow one (0.32 vs 0.86) and the bars grow by d + 0.54 (owner 2026-10-15: "the frets don't go all the
// way down"). The highway floor: the fret-wire bottom ends b (bar +28h world) are where the time axis
// (0x4B2C90), the neck fret numbers (0x4B1C00), the chord frames' origin edge, the stems of every note
// head and the highway surface are put. Stock puts b ~1.97 beyond the outer string. With inverted strings
// that is string 0's side, so the 7th lane lay 0.36 above the floor: fret numbers, 7th-string gems and the
// chord frames' lower edge sat on the 7th string. After the stock body of 0x58FB20 (which writes the world
// ends anew every call) b moves one string spacing further out when it lies on string 6's side; normal
// order (b beyond string 5) is untouched, except on a 6-string bass, whose high C lies beyond the G on the floor's
// side: there b moves one spacing beyond the G (FloorMove; before, the high C's gems sat under the highway). Only
// the game's own setters are used (Entity_SetTranslation 0x7ADF70, Entity_SetScale 0x7AE2C0). The locators a/b and
// the span data are never written, so gems, highway, fret numbers and wrong-fret FX stay stock. Guitars built with 6
// strings and 4-string bass are never touched.
//  - F1 (0x58FB20 entry): the stock body runs first; then, for a guitar built with 7/8 strings, the
//    bars are re-placed (the stock body has just put them back on the midpoint).
//  - F2 (in N5, per frame): applies when F1 could not (no attach points yet) and follows the live
//    the fret state (g_rsxr_frets_on, g_rsxr_fret_halflen). Nothing is switched per song, tuner or menu.
// Logs once per run: the applied numbers and a read-back of the bar nodes 30 frames later.
// Logic: frets.cpp (Compute, FloorShift, ChordFrame). Glue: hooks.cpp (F1 / F2, the floor, IN1, the
// chord-frame geometry, the per-guitar state), hooks_log.cpp (the log lines), hooks_wrap.cpp (F1, IN1, Wrap mid-hooks),
// registration.cpp (kRsxrFretHooks).

extern "C" {
__declspec(dllexport) extern uintptr_t g_rsxr_fn_setscale;  // X_SETSCALE 0x7AE2C0 EAX = entity, cdecl(float)
__declspec(dllexport) extern uintptr_t g_rsxr_frets_on;     // 1 (0 only in the emulator tests)
__declspec(dllexport) extern float g_rsxr_fret_halflen;     // kDefaultHalfLen
// F1, after the stock body of 0x58FB20 (cdecl, inside pushad). grid = the FretGrid (stock EAX).
// Returns at once (no Win32 call) unless the grid belongs to a guitar built with 7 or 8 strings.
__declspec(dllexport) void __cdecl Rsxr_FretsAfterApply(uint8_t* grid);
// IN1, after Rnd_Inlays_ApplyTransforms 0x59BA80 (which puts the inlay entity at the frame entity's translation;
// the dots are authored centred on strings 0-5): a guitar built with 7/8 strings gets the entity at that
// translation + (attach0 - attach1) / 2, the centre of 7 strings, in either string order. Part of fret_extend.
__declspec(dllexport) void __cdecl Rsxr_InlaysAfter(uint8_t* guitar, uint8_t* inlays);
}

namespace rsxrfrets {
constexpr float kDefaultHalfLen = 4.61f;  // half bar length estimate (bound radius 4.638, see the note)
using rsxr::kGuitarGrid;                  // FretGrid* (0x593043: EAX = [edi+10h])
using rsxr::kGuitarStrings;
constexpr uint32_t kBars = rsxr::kGridTail;  // FretGrid: fretBar[26] x 0x38
constexpr uint32_t kBarStride = 0x38;
constexpr uint32_t kBarALocal = 0x04;  // the fretbar locators as read (0x58F7F0)
constexpr uint32_t kBarBLocal = 0x10;
constexpr uint32_t kBarAWorld = 0x1C;
constexpr uint32_t kBarBWorld = 0x28;
constexpr uint32_t kBarEntity = 0x34;
constexpr int kMaxBars = 26;
using rsxr::kMaxGuitars;
constexpr int kVerifyFrames = 30;  // N5 frames after an apply before the node read-back

struct Extend {
    float shift[3];  // added to each bar's stock midpoint: (attach0 - attach1) / 2
    float scale;     // uniform scale k = (L + d/2) / L
    float spacing;   // d = |attach0 - attach1|
};

// Pure. a0/a1 = string 0/1 attach points (local; only their difference is used, so lefty and
// inverted layouts are handled by the attach points themselves). False (out untouched) for a
// degenerate spacing or half length.
// lanes = the strings beyond the stock six (1 or 2): the bar reaches that many spacings beyond string 0. far = the
// stock far string (5 on a guitar, 3, the G, on a bass) and farLanes the strings beyond it (1: a 6-string bass's high
// C).
bool Compute(const float a0[3], const float a1[3], const float mid[3], float halfLen, int lanes, Extend* out,
             int far = 5, int farLanes = 0);
// [neck] spacing_* (pure): the strings a0 / a1 were scaled by k about the string-axis origin; mid (a stock bar's
// midpoint) gets the same scale along that axis, and moved = its new position - the old one.
void ScaleBarMid(const float a0[3], const float a1[3], float k, float mid[3], float moved[3]);
// The highway floor: a0 / a1 = string 0 / 1 attach points, b = a fret wire's bottom end (all guitar-local). b beyond
// string 0 on string 6's side -> true and out = lanes * (a0 - a1) (`lanes` string spacings further out: 1 on a
// 7-string, 2 on an 8-string guitar); else false.
bool FloorShift(const float w0[3], const float w1[3], const float b[3], int lanes, float out[3]);
// The floor of an instrument with lanes beyond either stock outer string: `nearLanes` beyond string 0 (ExtraLanes),
// `farLanes` beyond the stock far string `far` (5, bass 3; a 6-string bass's high C). The floor moves out on whichever
// side it lies, so the outer lane there stays above it. Returns the spacings moved, -1 for degenerate spacing,
// or 0 for valid spacing needing no movement (out untouched for either).
int FloorMove(const float a0[3], const float a1[3], const float b[3], int nearLanes, int far, int farLanes,
              float out[3]);
// The fret range glow (FRG1): `p` = one end of a fret wire (world), w0 / w1 = string 0 / 1 (world). An end beyond
// string 0 (the extra strings' side) moves `lanes` string spacings further out (1..2), like the extended bar it lies
// on; the other end stays. False when p was not moved.
bool GlowEnd(const float w0[3], const float w1[3], int lanes, float p[3]);
// The same on the floor's side, where FloorMove may or may not have moved the end yet: places an end beyond string 0
// exactly `beyond` string spacings past it (the stock margin plus the extra lanes), whatever it was before; the other
// end stays. False when p is not beyond string 0 or was already there.
bool GlowEndAt(const float w0[3], const float w1[3], float beyond, float p[3]);
// The glow's bars and box are fixed-size effects made for the stock strings, which span kStockStringsWidth on a guitar
// (5 x 1.608) and a bass (3 x 2.68) alike, plus the margin beyond the outer strings (the fret wire ends). The game
// centres each one between the wire ends (FRG1 moved them); the uniform scale (FRG2 / FRG2B / FRG3) that makes it span
// `lanes` string spacings of `spacing` plus `margin` (world units) on both sides instead: 1 when that is no wider (a
// piece too long is harmless), at most 3.
constexpr float kStockStringsWidth = 8.04f;
float GlowStretch(int lanes, float spacing, float margin);

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
// floorLanes = the spacings FloorMove has moved the world floor by, on b's side (0: not moved). False for a
// degenerate geometry (out untouched).
struct FrameFit {
    float scale;
    float shift;
    float origin;   // diagnostics: the origin edge as placed now, in p (0 at string 0, string i at -i d, + towards
                    // string 6)
    float margin;   // m
    float spacing;  // d
    float bLocal[3], bWorld[3], a0[3];  // diagnostics, set by RsxrFrets_ChordFrame: bar 0 locator B (local / the world
                                        // end as placed now), string 0 attach
};

// far / farLanes as Compute: the stock frame covers lanes 0-`far` (5, bass 3); farLanes (0..1) more lanes beyond it (a
// 6-string bass's high C). A 5/6-string bass passes top 6: its low B lies one lane beyond string 0, like a 7th string.
bool ChordFrame(const float a0[3], const float a1[3], const float b[3], int floorLanes, int top, FrameFit* out,
                int far = 5, int farLanes = 0);
}  // namespace rsxrfrets

// B2: ChordFrame for the guitar `guitar` (Rnd_Guitar*): reads its attach points and bar 0, and whether F1 moved its
// floor. False when the guitar's strings / bars are not there or the geometry is degenerate.
bool RsxrFrets_ChordFrame(const uint8_t* guitar, int top, rsxrfrets::FrameFit* out);

// F2, from N5 (Rsxr_S7Update) every frame for its guitar: want = the guitar was built with 7/8 strings.
// With g_rsxr_frets_on = 0 (tests) an applied guitar is restored at its next frame and nothing else is touched; a new
// g_rsxr_fret_halflen is applied again at the next frame.
void RsxrFrets_Frame(uint8_t* guitar, bool want);
// The guitar is being destroyed (its GuitarStrings are freed): drop its state slot.
void RsxrFrets_Forget(const uint8_t* guitar);
// Song setup: the frets log lines may appear again; drops the glow bars FRG2 has not scaled yet.
void RsxrFrets_ResetLog();

// Overlay status: guitars with frets state, guitars whose bars are applied now.
struct RsxrFretsStats {
    int guitars, applied;
};

void RsxrFrets_Stats(RsxrFretsStats* out);

// ---- overlay (settings.cpp)

// The fret extend status row (platform/uilayout.h).
struct RsxrUiTable;
extern const RsxrUiTable kRsxrFretUi;
