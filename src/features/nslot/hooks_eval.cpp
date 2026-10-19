// Mid-hooks of the native N-slot chord detection and tracking that read or fill the tracker's Play_NoteEval (nslot.h
// kEv*, tracker.h): the Lua and whole-chord matcher arrays, the empty tails, the chord string -> note mask, the child
// eval slots of strings 6/7, and the chord pluck of string 6.
//
// Converted from naked stubs (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_nslot.py). Light mode
// (per note / tracker update, integer only, LIGHT_CALLBACKS in tools/tests/emu_harness.py), except PL1: it calls the
// native pluck copy, which uses floating point (full mode, once per chord hit).
#include "features/nslot/nslot.h"
#include "features/nslot/tracker.h"
#include "features/string7/string7.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_LU3 = 0, g_rs7_resume_WC2 = 0, g_rs7_resume_CS1 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PL1 = 0, g_rs7_resume_CC3 = 0, g_rs7_resume_CH3 = 0;
}

namespace {
constexpr uint32_t kNoSlot = 0xFFFFFFFF;

void Store32(uint32_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(address) = value;
}

void Store16(uint32_t address, uint16_t value) {
    *reinterpret_cast<volatile uint16_t*>(address) = value;
}
}  // namespace

// LU3 - Lua IsChordBeingPlayed passes int32[8]. 0x40213B in the Lua binding 0x402100 (its six MIDI arguments are
// stored to EBP-20h..-0Ch; LU1/LU2 moved the /GS cookie from EBP-8 to the free EBP-24h): targets 6 and 7 (EBP-8,
// EBP-4) = -1, so 0x4E9470 gets an int32[8]; the thunk replays `mov [ebp-10h],ecx; mov ecx,[ebp+8]`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_LU3(Context& c) {
    Store32(c.ebp - 8, kNoSlot);
    Store32(c.ebp - 4, kNoSlot);
}

// WC2 - whole-chord tracker update reads the eval's int32[8]. 0x87632F in the tracker's whole-chord update 0x8762D0
// (EDI = the eval; WC1 grew the frame by 20h): instead of the stock six targets at EBP-20h the matcher calls get the
// eval's midi[8] (+284h: the template copy's midi[6] + the N-slot tails) copied to EBP-44h (WC3-WC7 repoint the LEAs).
// Does `lea esi,[...]; mov eax,ebx` with ESI = the new array; the two stores of the stock copy it replaces ([ebp-10h],
// [ebp-0Ch]) belong to the old array. Continues at the BassAdj call 0x87633A.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_WC2(Context& c) {
    const uint32_t targets = c.ebp - 0x44;

    for (uint32_t i = 0; i < rs7ns::kSlots; ++i) {
        Store32(targets + 4 * i, Dword(c.edi + rs7ns::kEvMidi8 + 4 * i));
    }

    c.esi = targets;
    c.eax = c.ebx;
    c.jump = static_cast<uint32_t>(g_rs7_resume_WC2);
}

// CS1 - empty N-slot tails in Play_Eval_ClearState. 0x87443D in Play_Eval_ClearState 0x874350 (EAX = eval, ECX = 0,
// EDX = -1): after the replay of `mov [eax+0C1Ch],cx`, the tails get the stock clear's own registers: midi[6..7] =
// EDX, masks = ECX, frets/fingers/slides = EDX bytes, vibrato and the last name bytes = ECX, and no child evals for
// strings 6/7 (+0C1Eh = FFFFh).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CS1(Context& c) {
    using namespace rs7ns;
    const uint32_t eval = c.eax;

    Store16(eval + kEvChild67, 0xFFFF);
    Store32(eval + kEvMidi67, c.edx);
    Store32(eval + kEvMidi67 + 4, c.edx);
    Store32(eval + kEvMask67, c.ecx);
    Store32(eval + kEvMask67 + 4, c.ecx);
    Store32(eval + kEvFret67, c.edx);   // frets, fingers
    Store32(eval + kEvSlide67, c.edx);  // slideTo, slideUnpitchTo
    Store32(eval + kEvVib67, c.ecx);
    Store32(eval + kEvVib67 + 4, c.ecx);
}

// CC3 - chord string -> note conversion reads the C8 mask. 0x87488E in 0x8747F0 (EDX = ChordNotes, a C8; ECX =
// string): `test dword [edx+ecx*4],1000h` reads C8 mask[8] (+948h) instead; the following `seta` reads the flags.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CC3(Context& c) {
    c.Test(Dword(c.edx + c.ecx * 4 + rs7ns::kC8Mask), 0x1000, 32);
    c.jump = static_cast<uint32_t>(g_rs7_resume_CC3);
}

// CH3 - child eval slots of strings 6/7. 0x874D9C in Play_Tracker_AddNoteOrChord 0x874AB0, the store of a child eval
// slot (EAX) for chord string [EBP-0E0Ch] into the parent's subEval (EDX = parent slot * 30Ah, [EBP-0E00h] = parent
// slot, EBX = tracker): strings 0-5 run the replay (`add edx,[ebp-0E0Ch]; mov [ebx+edx*4+0C0Ch],eax`), strings 6/7
// store into the byte at parent eval +0C1Eh + (string - 6) (= tracker + slot * 0C28h + 0C26h), skipped when the
// parent got no slot (-1: stock would write in front of the pool). Flags there: the old stub's `cmp [ebp-0E00h],-1`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CH3(Context& c) {
    const uint32_t string = Dword(c.ebp - 0xE0C);
    const uint32_t parent = Dword(c.ebp - 0xE00);

    if (string < 6) {
        return;
    }

    c.Compare32(parent, kNoSlot);
    c.jump = static_cast<uint32_t>(g_rs7_resume_CH3);
    if (parent == kNoSlot) {
        return;
    }

    c.edx = c.ebx + c.edx * 4 + 0xC20;
    *reinterpret_cast<volatile uint8_t*>(c.edx + string) = static_cast<uint8_t>(c.eax);
}

// PL1 - pluck string 6 of a chord. 0x7E2EFE in the chord pluck 0x7E2DE0, right after its six-string pluck loop (EDI =
// T8 + 0Ah, past frets[5]; [EBP+8] = play session): string 6 of the chord (T8 frets[6] = [EDI+44h]) is plucked like a
// single string-6 note through Rs7_S7Pluck (SNG fret, display fret 1 for an open string, GuitarStrings =
// session+37Ch -> +0Ch); stock rejects string 6, so nothing happens otherwise. No register changes: the thunk then
// replays `mov al,[ebp-10h]; cmp al,0FFh` on the incoming EAX, so its upper bytes stay the game's (the old stub left
// EAX = the fret zero-extended).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PL1(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    const uint8_t fret = *reinterpret_cast<const uint8_t*>(c.edi + 0x44);

    if (fret == 0xFF) {
        return;
    }

    void* strings = reinterpret_cast<void*>(Dword(Dword(Dword(c.ebp + 8) + 0x37C) + 0x0C));

    Rs7_S7Pluck(strings, fret, fret ? fret : 1);
}

// BT1 - write the eval's N-slot tails at the epilogue of Play_Eval_Build 0x874180 (0x874340 `pop edi; pop esi; pop ebx;
// pop ebp; ret 14h`; EBX = EAX = the eval, [EBP+14h] = template, [EBP+18h] = ChordNotes, either may be NULL):
// Rs7Ns_OnEvalBuilt, then the epilogue replays (the relocator copies the closing `ret 14h`). Full mode: the tails are
// copied with memcpy (SSE), once per built eval. Converted from a naked stub (frozen at c15ca8d,
// tools/tests/test_midhook_strings_sites.py).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_BT1(Rs7Mid::Context& c) {
    Rs7Ns_OnEvalBuilt(Rs7Mid::Ptr(c.ebx), Rs7Mid::Ptr<const uint8_t>(Rs7Mid::Dword(c.ebp + 0x14)),
                      Rs7Mid::Ptr<const uint8_t>(Rs7Mid::Dword(c.ebp + 0x18)));
}

// AG1-AG3 - aggregate the child queries over all eight strings: the entries of the three unrolled child queries
// 0x875780 (fields 56h/76h, an empty child fails the chord), 0x875A10 (54h/74h, empty skipped, EDX = string pointer or
// 0) and 0x875C90 (55h/75h, same); ESI = parent eval, EDI = tracker. The callback answers for the whole function
// (Rs7Ns_Aggregate, one loop over the eight strings) and returns to its caller: AL = the result, every other register
// as on entry (stock writes only AL). Light mode: per tracked chord and frame, integer only. Converted from naked stubs
// (frozen at c15ca8d, tools/tests/test_midhook_strings_sites.py); the old stubs' flags were the helper's (dead after
// the `ret`).
namespace {
void Aggregate(Rs7Mid::Context& c, int field, int zeroOnEmpty, uint32_t stringPtr) {
    const uint8_t r = Rs7Ns_Aggregate(Rs7Mid::Ptr<const uint8_t>(c.esi), Rs7Mid::Ptr<const uint8_t>(c.edi), field,
                                      zeroOnEmpty, Rs7Mid::Ptr<const uint8_t>(stringPtr));

    c.eax = (c.eax & ~0xFFu) | r;
    Rs7Mid::Ret(c);
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AG1(Rs7Mid::Context& c) {
    Aggregate(c, 0x56, 1, 0);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AG2(Rs7Mid::Context& c) {
    Aggregate(c, 0x54, 0, c.edx);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AG3(Rs7Mid::Context& c) {
    Aggregate(c, 0x55, 0, c.edx);
}
