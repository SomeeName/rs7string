// The calls written into the publication preparation copies (tools/gen_publish.py, publish_specs.generated.h): call-in
// entries (Rs7Mid::CallIn, docs/ARCHITECTURE.md section 15 "Call-in entries") that run these callbacks with the copy's
// registers (EBP = the copy's game frame) in place of one stock instruction. They replaced the naked Rs7_Call_PubBaseG,
// _PubBaseB, _PubClassify and _PubWireCount (frozen at 304b9d3, tools/tests/test_midhook_callin2.py). Light: integer
// only; the classification (once per analysed note, a C helper) full.
#include "features/pitch/pitch.h"
#include "features/publish/hooks.h"
#include "platform/midhook.h"

using Rs7Mid::Byte;
using Rs7Mid::Dword;
using Rs7Mid::Ptr;

namespace {
// `sub eax,[table+ecx*4]` with its flags (the stock instruction's, over the native eight-entry table). ECX is not
// bounds-checked, as stock and the old helper (raw read).
void SubBase(Rs7Mid::Context& c, const int32_t* table) {
    const uint32_t base = Dword(reinterpret_cast<uintptr_t>(table) + 4 * c.ecx);

    c.Compare32(c.eax, base);
    c.eax -= base;
}
}  // namespace

extern "C" {
// PUBCL 0x8751FC / 0x875205 `sub eax,[ecx*4+12E8050h]` / `[ecx*4+12E8068h]`: the base pitch of string ECX from the
// eight-entry guitar / bass tables of the native pitch.
__declspec(dllexport) void __cdecl Rs7_CallIn_PubBaseG(Rs7Mid::Context& c) {
    SubBase(c, g_rs7_pitch_gbase);
}

__declspec(dllexport) void __cdecl Rs7_CallIn_PubBaseB(Rs7Mid::Context& c) {
    SubBase(c, g_rs7_pitch_bbase);
}

// PUBCL 0x87522C (in place of `mov [ebp-3Ch],0`; the copy then jumps to 0x8753D2): EDI = tracker, ESI = record;
// Rs7Publish_Classify picks the string / fret of the played MIDI [ebp-40h] over the published count, its result
// replaces [ebp-38h]. Registers kept; flags kept (the old helper left the C helper's, dead at the copy's jump).
__declspec(dllexport) void __cdecl Rs7_CallIn_PubClassify(Rs7Mid::Context& c) {
    const uint32_t slot = c.ebp - 0x38;

    *Ptr<int>(slot) = Rs7Publish_Classify(Ptr(c.edi), Ptr(c.esi), int(Dword(c.ebp - 0x40)), int(Dword(slot)));
}

// PUBWU 0x58E7A2 `mov [ebp+8],6`: EDI still holds the caller's grid; the string count the copy works with,
// [[grid]+24h]+0Bh, stored to [ebp+8]. Registers and flags kept.
__declspec(dllexport) void __cdecl Rs7_CallIn_PubWireCount(Rs7Mid::Context& c) {
    *Ptr<uint32_t>(c.ebp + 8) = Byte(Dword(Dword(c.edi) + 0x24) + 0x0B);
}
}

Rs7Mid::CallIn g_rs7_callin_PubBaseG{&Rs7_CallIn_PubBaseG, Rs7Mid::Mode::Light};
Rs7Mid::CallIn g_rs7_callin_PubBaseB{&Rs7_CallIn_PubBaseB, Rs7Mid::Mode::Light};
Rs7Mid::CallIn g_rs7_callin_PubClassify{&Rs7_CallIn_PubClassify, Rs7Mid::Mode::Full};
Rs7Mid::CallIn g_rs7_callin_PubWireCount{&Rs7_CallIn_PubWireCount, Rs7Mid::Mode::Light};

// ---- the native pitch readers' copies (tools/gen_publish_pitch.py, publish_pitch.generated.h; hooks_pitch.cpp)

extern "C" {
// PUBAC: Rs7PublishPitch_ChordTail(out8 = EBP-94h, template = [EBP-44h], tracker = [EBP-3Ch]) fills out8[6..7] with the
// T8 template's MIDI of strings 6/7 (-1 when the template has none or the detector's count is below the string).
// Registers and flags kept (the old helper's popad kept the registers, the C helper left its flags; the copy's next
// instruction is a `mov`, and the old flags were already the C helper's). Full: per analysed chord, a C helper.
__declspec(dllexport) void __cdecl Rs7_CallIn_PubChordTail(Rs7Mid::Context& c) {
    Rs7PublishPitch_ChordTail(Ptr<int32_t>(c.ebp - 0x94), Ptr<const uint8_t>(Dword(c.ebp - 0x44)),
                              Ptr<const uint8_t>(Dword(c.ebp - 0x3C)));
}

// PUBAN, after its real-detector allocation guard: the inverse pitch (MIDI -> fret) for the tail tuning. ECX = played
// MIDI, EDX = string, ESI = the tail tuning (or 0), [esp+4] = instrument (the caller pops it) -> EAX = ECX = MIDI -
// base[string] (-1 past string 7) - tail[string]. The inverse counterpart of I1, whose stock side-store key stays
// detector+1340h. Flags: the old helper's last `sub` / `test`, rewritten by the caller's `add esp,4` (dead). Light.
__declspec(dllexport) void __cdecl Rs7_CallIn_ReverseTail(Rs7Mid::Context& c) {
    const uint32_t s = c.edx;
    uint32_t fret = c.ecx - (s <= 7 ? uint32_t(g_rs7_pitch_gbase[s]) : 0xFFFFFFFFu);

    if (c.esi) {
        fret -= uint32_t(int32_t(*Ptr<const int16_t>(c.esi + s * 2)));
    }

    c.eax = c.ecx = fret;
}
}

Rs7Mid::CallIn g_rs7_callin_PubChordTail{&Rs7_CallIn_PubChordTail, Rs7Mid::Mode::Full};
Rs7Mid::CallIn g_rs7_callin_ReverseTail{&Rs7_CallIn_ReverseTail, Rs7Mid::Mode::Light};
