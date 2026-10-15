#pragma once
#include <cstdint>

// Native N-slot GuitarStrings (issue #18 stage 4, docs/ARCHITECTURE.md), a member of the native 7-string unit
// (no ini switch since 1.0). The 7th string is one more stock string object (and ghost) owned by the
// GuitarStrings itself, driven by the stock string loops; no mirror of string 0.
//
// Owners (allocated by the guitar ctor 0x5925F0 while the switch is on, sites GSA1/GSC1/GIA1):
//   GuitarStrings 0x40 -> kGsSize: stock prefix kept (main[0..5] at +4, ghost[0..5] at +1Ch stay valid
//     aliases, so unwidened stock readers keep working); +40h main[8], +60h ghost[8] (the stock ctor's
//     six + two more of each kind, constructed by GSC1), +80h glow[8] (the renderer glow ramp of the
//     native UpdateGlow), +0A0h tail, marker 'R7' at +3Ah (stock padding) and magic at +0ACh.
//   InstrumentInfo 0x38 -> kInfoSize: +40h active count, +41h loaded count (stock +0Bh untouched, so
//     every consumer outside the native loops keeps 6), +44h magic.
// Loops: the stock functions are copied (core/fncopy.h, tools/gen_fncopy.py) with their array
// displacements moved to +40h/+60h and their count byte to +40h/+41h; their entries (GS* sites) run
// the copy for a native GuitarStrings and the stock code otherwise.
// Counts: loaded = 7 for a guitar (6 stock strings), else the stock count (bass 4); active = 7 while
// the guitar shows the 7th string (N5's per-guitar 7-string rule, Rs7Gs_Frame), else 6. Loaded-count
// loops: bind, load, unload, transforms, entity pass, reset, InitColors, free (8). Active-count loops:
// fade in/out, update, pluck, vibration, section dimming / next-note glow choice, headstock segment
// colours. A guitar shows the 7th string by raising the count (string 6 un-culled, its ClipFade set to
// the strings' fade state) and hides it by lowering it (string 6 and its ghost culled).

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_native_strings;  // set before patching; a startup check may clear it
__declspec(dllexport) extern uintptr_t g_rs7_fn_attach;       // X_ATTACH 0x594530 ECX = info, cdecl(s, &vec3) -> AL
// Strings 6/7: [mode][string - 6][state 1 lit, 2 dimmed, 3 glow][rgb]; the glow entry is read by
// Rs7_Call_GlowColour (the lit / dimmed state colours come from the native colour matrix since GSCL went in 1.0).
__declspec(dllexport) extern float g_rs7_ns_rgb[2 * 2 * 3 * 3];
// v_positionString of the 7th/8th strings' atlas row (main and ghost), for 0x541880 (GRM3).
__declspec(dllexport) extern float g_rs7_ns_v[2];

// Called from the stubs (cdecl; none throws).
// Entry dispatch of a copied function: 1 = run the native copy. kind = Rs7GsKind; reg = the
// GuitarStrings register (or the renderer / context the kind names), a1/a2 = the first two stack
// arguments.
__declspec(dllexport) int __cdecl Rs7Gs_Use(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2);
// GSC1: the stock ctor on a GuitarStrings the guitar just allocated (kGsSize while the switch is on).
__declspec(dllexport) uint8_t* __cdecl Rs7Gs_Construct(uint8_t* guitar, uint8_t* gs);
// GIA1: an InstrumentInfo the guitar just allocated with kInfoSize (before its stock ctor).
__declspec(dllexport) void __cdecl Rs7Gs_InfoAllocated(uint8_t* info);
// GIZ1: the free size of an InstrumentInfo (unregisters a native one).
__declspec(dllexport) uint32_t __cdecl Rs7Gs_InfoFreeSize(uint8_t* info);
// Registry membership precedes every InstrumentInfo tail read or publication write.
__declspec(dllexport) int __cdecl Rs7Gs_InfoNative(const uint8_t* info);
// Both capacity extras have an entity and the stock loaded-model flag (+50h).
__declspec(dllexport) int __cdecl Rs7Gs_ExtraReady(uint8_t* guitar);
__declspec(dllexport) int __cdecl Rs7Gs_ExtraWhy(uint8_t* guitar);
// GRM1: how many main strings 0x541880 collects meshes for (renderer +4 = GuitarStrings).
__declspec(dllexport) uint32_t __cdecl Rs7Gs_MeshCount(uint8_t* gs);
// Rs7_Call_HsLoaded / Rs7_Call_HsSeg: the headstock segment colour handle of string s >= 6 (the
// edited model's string_size_7, core/assetload.h), nullptr without one.
__declspec(dllexport) uint8_t* __cdecl Rs7Gs_SegHandle(uint8_t* guitar, uint32_t s);
// Rs7_Call_Attach (s >= 6): the attach point of an added string, extrapolated from strings 0 and 1
// of the stock query. 1 = written.
__declspec(dllexport) int __cdecl Rs7Gs_AttachExt(uint8_t* info, uint32_t s, float* out);
// Rs7_Call_Shape (s >= 6), after the stock 0x5996A0: the added string's atlas row.
__declspec(dllexport) void __cdecl Rs7Gs_ShapeDone(uint8_t* obj, uint32_t s);
}

enum Rs7GsKind : uint32_t {
    kGsFree,
    kGsBind,
    kGsUnload,
    kGsLoad,
    kGsFadeOut,
    kGsFadeIn,
    kGsXform,
    kGsWorld,
    kGsUpdate,
    kGsReset,
    kGsPluck,
    kGsVib,
    kGsInitCol,
    kGsDim,
    kGsGlow,
    kGsHeadCol,
    kGsKinds
};

namespace rs7gs {
constexpr uint32_t kStockGsSize = 0x40;
constexpr uint32_t kMain = 0x40;
constexpr uint32_t kGhost = 0x60;
constexpr uint32_t kGlow = 0x80;
constexpr uint32_t kTail =
    0xA0;  // u8 frames without the 7th string, u8 load generation, u8 culled generation, u8 complete
constexpr uint32_t kGuitar = 0xA4;
constexpr uint32_t kInfo = 0xA8;
constexpr uint32_t kMagicOff = 0xAC;
constexpr uint32_t kGsSize = 0xB0;
constexpr uint32_t kMarkerOff = 0x3A;    // u16, stock padding between +39h and +3Ch
constexpr uint16_t kMarker = 0x3752;     // "R7"
constexpr uint32_t kMagic = 0x47375352;  // "RS7G"
constexpr int kCapacity = 8;
constexpr int kStock = 6;
constexpr int kLowerFrames = 45;  // frames without the 7th string before it is lowered (as the PegSet)
constexpr uint32_t kStockInfoSize = 0x38;
constexpr uint32_t kInfoActive = 0x40;
constexpr uint32_t kInfoLoaded = 0x41;
constexpr uint32_t kInfoMagicOff = 0x44;
constexpr uint32_t kInfoSize = 0x48;
constexpr uint32_t kInfoMagic = 0x49375352;  // "RS7I"

// Layout checks (pure). IsNative reads +3Ah first (inside the stock 0x40 bytes), then the magic.
bool IsNative(const uint8_t* gs);

inline uint8_t* Child(const uint8_t* gs, int s, bool ghost) {
    return *reinterpret_cast<uint8_t* const*>(gs + (ghost ? kGhost : kMain) + 4 * s);
}

// The stock ctor's layout -> the native one, given the two added objects of each kind (pure): arrays
// filled (prefix aliases kept), glow zeroed, tail, marker and magic.
void InitTail(uint8_t* gs, uint8_t* guitar, uint8_t* info, uint8_t* const extraMain[2], uint8_t* const extraGhost[2]);
// The counts the native loops run (pure): loaded = 7 for a 6-string instrument (with the 7th string
// line on), else the stock count; active = clamp(active, stock count .. loaded).
void RefreshCounts(uint8_t* info, bool seventh);

// The attach point of added string s (6: 7th, 7: 8th) from strings 0 and 1 (pure): a0 + (s - 5) * (a0 - a1),
// the next slot of the stock row (the spacing of strings 0/1 continues). The headstock segment
// string_size_7 ends at this point (the edited model's pitch is the same), so the 7th neck string
// and its segment meet like strings 0-5 (tools/tests/test_nutjoint.py).
inline void ExtrapolateAttach(const float a0[3], const float a1[3], uint32_t s, float out[3]) {
    const float k = float(int(s) - 5);

    for (int i = 0; i < 3; ++i) {
        out[i] = a0[i] + k * (a0[i] - a1[i]);
    }
}
}  // namespace rs7gs

// From N5 (Rs7_S7Update) every frame on the game thread, for a native GuitarStrings: `want` = this
// guitar shows the 7th string. Raises / lowers the active count. True when it handled the guitar.
// With native_publish (the guitar's string count is its build count, C-lite) `want` is not used: the
// guitar shows the count it was built with.
bool Rs7Gs_Frame(uint8_t* guitar, uint8_t* gs, bool want);
// String s (>= 6) of a native GuitarStrings if it is active, has its model and shader params; else
// nullptr. ghost: the ghost string.
uint8_t* Rs7Gs_Active(uint8_t* gs, int s, bool ghost);
// Startup (before any site is written): builds the copies; false = native strings off (logged).
bool Rs7Gs_BuildCopies();

struct Rs7GsStats {
    int native, active7, raised, lowered;
};

void Rs7Gs_Stats(Rs7GsStats* out);
