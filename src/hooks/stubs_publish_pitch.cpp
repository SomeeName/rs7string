// Publication stubs, part 3: guarded native pitch readers (core/publish_pitch.h). With the publication ports
// on, three stock functions that turn a played MIDI note into a string / fret (or back) have native copies
// (tools/gen_publish_pitch.py) that read the eight-entry tuning tail; the stock entries below choose copy or
// original. Integer-only helpers: live x87 state of the callers stays intact.
//
//   PUBNL PUBAN PUBAC          entry dispatchers of the copies of 0x4E6E90 (matcher), 0x875430 (tracker),
//                              0x8755C0 (chord-mistake template)
//   Rs7_Call_PubChordTail      call written into a copy: the chord tail
//   Rs7_Publish_ReverseTail    inverse pitch (MIDI -> fret) for the tail tuning
//   Rs7_Publish_ZeroRaw        wrapper of the standard-zero expected pitch (sites PUBZ0 / PUBZ1)
#include "core/pitch8.h"
#include "core/publish.h"
#include "core/publish_pitch.h"
#include "hooks/stub_macros.h"

// Entry dispatch with the original stack argument: reserve the return slot (push eax), save flags and
// GPRs, call Rs7PublishPitch_Target(kind, [original esp+4]) (the object whose layout it checks), store the
// answer over the slot, restore, `ret` into the copy or the original.
#define RS7_PUBPITCH_ENTRY(ID, KIND)                                                                                   \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm push eax __asm pushfd __asm pushad __asm push dword                                                      \
            ptr[esp + 44] __asm push KIND __asm call Rs7PublishPitch_Target __asm add esp,                             \
            8 __asm mov[esp + 36], eax __asm popad __asm popfd __asm ret                                               \
    }
// PUBNL 0x4E6E90: the original [esp+4] is the matcher.
RS7_PUBPITCH_ENTRY(PUBNL, 0)
// PUBAN 0x875430: the original stack argument is the tracker.
RS7_PUBPITCH_ENTRY(PUBAN, 1)
// PUBAC 0x8755C0: ECX is the template; the original stack argument is the tracker.
RS7_PUBPITCH_ENTRY(PUBAC, 2)

// PubChordTail - copy-local CALL (in the PUBAC copy): Rs7PublishPitch_ChordTail(out8 = EBP-94h, template =
// [EBP-44h], tracker = [EBP-3Ch]) fills out8[6..7] with the T8 template's MIDI of strings 6/7 (-1 when the
// template has none or the detector's count is below the string).
RS7_STUB(Rs7_Call_PubChordTail) {
    __asm {
        pushad
        push dword ptr [ebp-3Ch]
        push dword ptr [ebp-44h]
        lea eax,[ebp-94h]
        push eax
        call Rs7PublishPitch_ChordTail
        add esp,12
        popad
        ret
    }
}

// Only called from PUBAN after its real-detector allocation guard. ECX=played MIDI,
// EDX=string, ESI=tail tuning, stack instrument; caller pops4, result EAX=fret.
// This is the inverse counterpart to I1, whose stock side-store key stays detector+1340h.
RS7_STUB(Rs7_Publish_ReverseTail) {
    __asm {
        or eax,-1
        cmp edx,7
        ja base_done
        mov eax,dword ptr [g_rs7_pitch_gbase+edx*4]
    base_done:
        sub ecx,eax
        test esi,esi
        jz result
        movsx eax,word ptr [esi+edx*2]
        sub ecx,eax
    result:
        mov eax,ecx
        ret
    }
}

// Rs7_Publish_ZeroRaw - the standard-zero expected-pitch wrapper (sites PUBZ0 0x402AC8 call, PUBZ1 0x48DC10
// jmp), in place of a call of 0x873F80 with the standard (zero) tuning: guitar strings 6/7 with the
// publication ports on take Rs7_Pitch_Raw8 over the all-zero tuning g_rs7_publish_zero8; everything else
// goes to the original.
RS7_STUB(Rs7_Publish_ZeroRaw) {
    __asm {
        cmp dword ptr [g_rs7_publish_ports],0
        je legacy
        cmp esi,6
        jb legacy
        cmp esi,7
        ja legacy
        cmp edx,2
        je legacy
        mov ecx,offset g_rs7_publish_zero8
        jmp Rs7_Pitch_Raw8
    legacy:
        // Original 873F80 entry; direct original destination preserves S1 legacy behavior.
        push 873F80h
        ret
    }
}
