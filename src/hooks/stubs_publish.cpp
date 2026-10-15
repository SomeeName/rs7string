// Publication stubs of the native string count (core/publish.h), part 2: entry dispatch of the preparation
// function copies and the calls written into them. With the publication ports on, three stock functions run
// as relocated copies (tools/gen_publish.py) that read the published count; the stock entries below choose
// the copy or the original. Part 1 (the grid) is stubs_publish_grid.cpp, the pitch readers stubs_publish_pitch.cpp.
//
//   PUBCL PUBTO PUBWU   entry dispatchers of the copies of 0x8750C0 (tracker), 0x4E58E0 (matcher),
//                       0x58E790 (the caller's FretGrid)
//   Rs7_Call_Pub*       calls written INTO those copies (not sites)
#include "core/pitch8.h"
#include "core/publish.h"
#include "hooks/stub_macros.h"

// Entry dispatch (RS7_ENTRY_DISPATCH_KEEP_FLAGS, hooks/stub_macros.h) preserves GPRs, flags and the entry
// stack. Integer-only guard: live x87 stays live. Rs7Publish_Target(kind, reg) answers copy or original.
RS7_ENTRY_DISPATCH_KEEP_FLAGS(PUBCL, Rs7Publish_Target, 0, ecx)  // 8750C0 tracker
RS7_ENTRY_DISPATCH_KEEP_FLAGS(PUBTO, Rs7Publish_Target, 1, esi)  // 4E58E0 matcher
RS7_ENTRY_DISPATCH_KEEP_FLAGS(PUBWU, Rs7Publish_Target, 2, edi)  // 58E790 caller's FretGrid

// PubClassify - copy-local CALL at 87522C (in the PUBCL copy). EBP is the original game frame, EDI the
// tracker, ESI the record: Rs7Publish_Classify (exact scalar form of 875240..8753D2, core/publish.h) picks
// the string / fret of the played MIDI over the published count; its result replaces [ebp-38h].
RS7_STUB(Rs7_Call_PubClassify) {
    __asm {
        pushad
        push dword ptr [ebp-38h]
        push dword ptr [ebp-40h]
        push esi
        push edi
        call Rs7Publish_Classify
        add esp, 16
        mov dword ptr [ebp-38h], eax
        popad
        ret
    }
}

// PubBaseG / PubBaseB - copy-local CALLs at 8751FC / 875205: subtract the base pitch of string ECX from EAX,
// read from the eight-entry guitar / bass base tables of the native pitch (g_rs7_pitch_gbase / _bbase).
RS7_STUB(Rs7_Call_PubBaseG) {__asm {sub eax, dword ptr[g_rs7_pitch_gbase + ecx * 4]} __asm ret} RS7_STUB(
    Rs7_Call_PubBaseB) {
    __asm {sub eax, dword ptr[g_rs7_pitch_bbase + ecx * 4]} __asm ret}
// PubWireCount - copy-local CALL at 58E7A2 (in the PUBWU copy). EDI still holds the caller's grid (pushed,
// not overwritten): the string count the copy works with, [[grid]+24h]+0Bh, stored to [ebp+8].
RS7_STUB(Rs7_Call_PubWireCount) {
    __asm {
        push ecx
        mov ecx, dword ptr [edi]
        mov ecx, dword ptr [ecx+24h]
        movzx ecx, byte ptr [ecx+0Bh]
        mov dword ptr [ebp+8], ecx
        pop ecx
        ret
    }
}
