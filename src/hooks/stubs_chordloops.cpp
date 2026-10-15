// Chord loops over eight strings ([chords] native_chordloops, evidence: docs/ARCHITECTURE.md section 7).
// Written only with the switch on; all CL* sites opt in together (the stock CLB*/CLCT*/CLNF* sites are
// byte patches). C8/T8 prefixes remain unchanged, tracker child queries already cover eight slots.
//
//   CLBM  chord bend mask test: C8 mask[8]           CLGU  chord chart bend -> ghost string (6/7)
//   CLBA  bend data address: C8 bends[8]             CLGE  chord ghost bend release (6/7)
//   CLVB  stop-pluck path (6/7)
//
// The shape of CLGU / CLGE / CLVB: strings 0-5 replay the stock instructions exactly; strings >= 6 ask
// Rs7ChordLoops_Target (inside pushad, integer only so live x87 bend values survive) for the string object
// (main or ghost) and join the stock code with it, or reject like a missing string.
#include "core/chordloops.h"
#include "hooks/stub_macros.h"

#define STUB(id) RS7_STUB(Rs7_Stub_##id)
RS7_RESUME_VAR(CLBM);
RS7_RESUME_VAR(CLBA);
RS7_RESUME_VAR(CLGU);
RS7_RESUME_VAR(CLGE);
RS7_RESUME_VAR(CLVB);

// CLBM - chord bend mask test over the C8 mask[8].
// 7A62D4: retain TEST flags for SETA CL; C8 mask[8].
STUB(CLBM) {
    __asm {
        test dword ptr [edx+eax*4+948h], 1000h
        jmp g_rs7_resume_CLBM
    }
}

// CLBA - bend data address from the C8 bends[8].
// 7E3C62: BendData32 stride unchanged, C8 bends[8] base +988h.
// The following 7E3C6C is an inbound target; never overwrite that instruction.
STUB(CLBA) {
    __asm {
        imul eax, eax, 184h
        lea eax, [eax+ecx+988h]
        jmp g_rs7_resume_CLBA
    }
}

// CLGU - chord chart bend on the ghost string, strings 6/7.
// 7E3ED5..7E3EE6: chord chart bend -> ghost. The lower six replay stock exactly.
// x87 bend values are live: the integer-only helper and PUSHAD do not touch them.
STUB(CLGU) {
    __asm {
        cmp al, 6
        jb stock
        cmp dl, 0FFh
        je reject
        pushad
        movzx edx, al
        push 1
        push edx
        push ecx
        call Rs7ChordLoops_Target
        add esp, 12
        test eax, eax
        jz absent
        mov [esp+24], eax  // saved ECX
        popad
        movzx edx, al  // original index register
        push dword ptr [g_rs7_resume_CLGU]
        add dword ptr [esp], 4  // skip the stock ghost-prefix pointer read
        ret
    absent:
        popad
    reject:
        push 7E3F14h
        ret
    stock:
        cmp al, [esi+0Bh]
        jae reject
        cmp dl, 0FFh
        je reject
        movzx edx, al
        mov ecx, [ecx+edx*4+1Ch]
        push dword ptr [g_rs7_resume_CLGU]
        add dword ptr [esp], 4
        ret
    }
}

// CLGE - chord ghost bend release, strings 6/7.
// 7E4347..7E4353: chord ghost bend release. Keep EAX (tree node) and x87.
STUB(CLGE) {
    __asm {
        cmp cl, 6
        jb stock
        pushad
        movzx ecx, cl
        push 1
        push ecx
        push edx
        call Rs7ChordLoops_Target
        add esp, 12
        test eax, eax
        jz absent
        mov [esp+24], eax
        popad
        jmp g_rs7_resume_CLGE
    absent:
        popad
    reject:
        push 7E4373h
        ret
    stock:
        cmp cl, [esi+0Bh]
        jae reject
        movzx ecx, cl
        mov ecx, [edx+ecx*4+1Ch]
        jmp g_rs7_resume_CLGE
    }
}

// CLVB - stop-pluck path, strings 6/7.
// 7E4178..7E4189: stop-pluck path (not a vibrato amount reader). EAX = owner,
// CL = string; call 5992E0 with ESI = real native main string, never prefix+4*s.
STUB(CLVB) {
    __asm {
        cmp cl, 6
        jb stock
        pushad
        movzx ecx, cl
        push 0
        push ecx
        push eax
        call Rs7ChordLoops_Target
        add esp, 12
        test eax, eax
        jz absent
        mov [esp+4], eax  // saved ESI
        popad
        jmp g_rs7_resume_CLVB
    absent:
        popad
    reject:
        push dword ptr [g_rs7_resume_CLVB]
        add dword ptr [esp], 5  // reject: skip the original stop-pluck call
        ret
    stock:
        cmp cl, [edx+0Bh]
        jae reject
        movzx ecx, cl
        mov esi, [eax+ecx*4+4]
        jmp g_rs7_resume_CLVB
    }
}
