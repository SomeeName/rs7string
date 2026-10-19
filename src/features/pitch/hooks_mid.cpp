// Mid-hook callbacks of the pitch feature (pitch.h): the native pitch storage ([neck] native_pitch), whose matcher /
// detector tails are initialised when a matcher is created (PM0), a detector is reset (PR0) and a matcher is given a
// tuning (PM1), cold, full mode; the two exits of Play_Detector_SetTuning (PT1, S3); and the inverse pitch of string
// 6 (I1). They replaced hand-written naked stubs
// (docs/ARCHITECTURE.md, "Mid-hook conversion"); equivalence against the frozen stubs:
// tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "features/pitch/pitch.h"
#include "platform/midhook.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PM0 = 0;

// PM0 0x4EADDD `mov dword [eax+40h],-1` in Play_DetectInput_Init: EAX = the new matcher (its stock fields set above),
// ST0 = the fldz stored next. The callback does the stock store itself, so Rs7Pitch_MatNew sees the finished stock
// fields as before, and skips the replay. Flags are dead (fstp follows): the hook leaves the incoming ones.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PM0(Rs7Mid::Context& c) {
    *reinterpret_cast<uint32_t*>(c.eax + 0x40) = 0xFFFFFFFFu;
    Rs7Pitch_MatNew(reinterpret_cast<uint8_t*>(c.eax));
    c.jump = uint32_t(g_rs7_resume_PM0);
}

// PR0 0x4DC07B `mov eax,[ebx+11F0h]` in Play_Detector_Reset 0x4DBF20, the join after the guitar / bass open-MIDI
// writers: EBX = detector (tuning[6] zeroed, instrument +11ECh set). Then the replay. Flags are dead.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PR0(Rs7Mid::Context& c) {
    Rs7Pitch_DetReset(reinterpret_cast<uint8_t*>(c.ebx));
}

// PM1 0x4E85E5 in Play_Matcher_SetTuning 0x4E85A0, after the six-entry copy loop (the vector has >= 6 entries): ESI =
// matcher, EDX = vector, EBX still pushed. Then the replay `mov eax,[esi+4]; pop ebx; test eax,eax` (the je after it
// reads ZF).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PM1(Rs7Mid::Context& c) {
    Rs7Pitch_MatTuned(reinterpret_cast<uint8_t*>(c.esi), reinterpret_cast<const Rs7Int16Vec*>(c.edx));
}

// The two exits of Play_Detector_SetTuning 0x4E2590 (EAX = detector, ESI = the std::vector<int16> tuning it just
// copied; cold: once per tuning change), `mov [eax+1356h],<reg16>; ret`: jmp_atomic8 sites (the 5-byte jmp is one
// locked write, the replay would be the whole `mov; ret`). The callback does the store itself, so the helper sees the
// finished detector as before, calls the helper and returns for the function (Rs7Mid::Ret). Flags are dead after the
// return (the old stubs left the helper's).
namespace {
// The displaced `mov [eax+1356h],<reg16>`: the last tuning entry.
void StoreLastEntry(const Rs7Mid::Context& c, uint32_t entry) {
    *reinterpret_cast<uint16_t*>(c.eax + 0x1356) = uint16_t(entry);
}

const Rs7Int16Vec* Tuning(const Rs7Mid::Context& c) {
    return reinterpret_cast<const Rs7Int16Vec*>(c.esi);
}
}  // namespace

// PT1 0x4E26BA, the bass exit (`mov [eax+1356h],dx`): fills the native detector tail ([neck] native_pitch).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PT1(Rs7Mid::Context& c) {
    StoreLastEntry(c, c.edx);
    Rs7Pitch_DetTuned(reinterpret_cast<uint8_t*>(c.eax), Tuning(c));
    Rs7Mid::Ret(c);
}

// ---- the song's 7-string mode ----
// S3 0x4E263B, the guitar exit (`mov [eax+1356h],cx`): a 7-entry tuning lowers the detector's lowest open MIDI.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_S3(Rs7Mid::Context& c) {
    StoreLastEntry(c, c.ecx);
    Rs7_OnDetectorTuned(reinterpret_cast<void*>(c.eax), Tuning(c));
    Rs7Mid::Ret(c);
}

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_I1 = 0;

// I1 0x874056 in Play_MidiToFret 0x874050 (inverse pitch feedback, from Play_AnalyzeNoteMistake 0x875430: once per
// analysed mistake, an event, so full mode is within the cost budget): replays `sub eax,2; jz bass` (EAX = instrument,
// ECX = played MIDI, EDX = string, ESI = the detector tuning short* = the side-store key). Stock reads guitar-base[6] =
// bass-base[0] = 28 and tuning[6] past the six entries. String 6 of an active 7-string song: fret = MIDI - (35 + t6)
// (the inverse of S1 / S2) in ECX and EAX, then the stock `pop ebp; ret` (resume + 0Dh). String 6 without an active
// song: EAX = kNotActive (the old stub's leftover; the stock path after the resume overwrites EAX) and the stock code
// at the resume. Flags are dead at every continuation.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_I1(Rs7Mid::Context& c) {
    if (c.eax == 2 || c.edx != 6 || !c.esi) {
        return;
    }

    const int base = Rs7_Base6(reinterpret_cast<const void*>(c.esi));

    if (base == rs7::kNotActive) {
        c.eax = uint32_t(rs7::kNotActive);
        c.jump = uint32_t(g_rs7_resume_I1);
        return;
    }

    c.ecx -= uint32_t(base);
    c.eax = c.ecx;
    c.jump = uint32_t(g_rs7_resume_I1) + 0x0D;
}

// PE0 - the native expected-pitch function: the entry of 0x873E90 (EAX = instrument, EDX = string, [esp+4] = fret,
// [esp+8] = std::vector<int16>* tuning; plain `ret`, the caller pops 8), answered by the callback for the whole
// function. Bass (instrument 2) strings 0-3 read the bass table; any other instrument is the guitar, whose strings 0-7
// read the eight-entry base table (35 / 30 for strings 6 / 7); everything else base -1; the result tuning[string] +
// base + fret returns to the caller with ECX = the base, as the stock cases leave it. A string past the vector goes to
// the stock failure path 0x873F09 (push 119C8A8h; call 433650h) with the stock frame (`push ebp; mov ebp,esp`, EAX =
// the vector's begin). Light mode: per note, integer only. Converted from a naked stub (frozen at c15ca8d,
// tools/tests/test_midhook_asm_sites.py); flags are dead after the `ret` and at the failure path's `push`.
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PE0 = 0;

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PE0(Rs7Mid::Context& c) {
    const uint32_t string = c.edx;
    const bool bass = c.eax == 2;
    uint32_t base = 0xFFFFFFFFu;

    if (bass ? string <= 3 : string <= 7) {
        base = uint32_t(bass ? g_rs7_pitch_bbase[string] : g_rs7_pitch_gbase[string]);
    }

    const uint32_t vector = Rs7Mid::Dword(c.esp + 8);
    const uint32_t begin = Rs7Mid::Dword(vector);
    const uint32_t count = uint32_t(int32_t(Rs7Mid::Dword(vector + 4) - begin) >> 1);

    c.ecx = base;

    if (string < count) {
        c.eax = uint32_t(int32_t(*Rs7Mid::Ptr<const int16_t>(begin + 2 * string))) + base + Rs7Mid::Dword(c.esp + 4);
        Rs7Mid::Ret(c);
        return;
    }

    c.eax = begin;
    Rs7Mid::Push(c, c.ebp);
    c.ebp = c.esp;
    c.jump = uint32_t(g_rs7_resume_PE0) + 0x73;
}
