// Native capacity-8 next-note grid stubs (issue #18 stage 5 area 2, core/nextgrid.h). Written only with the native
// next-note grid (registry: kRs7NgNative), a member of the native 7-string unit.
#include "core/neck.h"
#include "core/nextgrid.h"
#include "hooks/stub_macros.h"
#include <cstdint>

// The grid (stock: 6 time rows on the stack of Rnd_NeckTargets_Update 0x7E4870) grows to 8 rows; the
// in-place byte sites NGB* move its offsets (registry_nextgrid.inc), these three stubs do what bytes cannot:
//   NG1  the cell index of Rnd_NextNoteGrid_Set (s*26+f, with the new 8-row base) and its reject test
//   NGP  the stack probe for the frame that grew past one page
//   NG9  spawn the string-6 targets after the consumer loop, and the tree-release replay
RS7_RESUME_VAR(NG1);
RS7_RESUME_VAR(NGP);
RS7_RESUME_VAR(NG9);
RS7_TARGET_VAR(NG9);

// NG1 - next-note grid cell address for 8 string rows.
// 0x7AA166 in Rnd_NextNoteGrid_Set 0x7AA140. ESI = grid, [ebp+8] string byte,
// [ebp+0Ch] fret byte, ST0 = the new time; the time-set insert already ran. Replaces `movzx ecx,[ebp+8]; movzx
// edx,[ebp+0Ch]; lea eax,[ecx+1Ah]; imul eax,eax,1Ah; add eax,edx` (16 bytes); stock continues with `lea
// edi,[esi+eax*4]` = &time cell. EAX = s*26 + f + 347h puts it at G + 0D1Ch + (s*26+f)*4. ECX = s and EDX = f
// are the stock values for the pointer store at 0x7AA184. s >= 8 or f >= 26: the stock reject tail 0x7AA198
// (`fstp st(0); pop edi; mov esp,ebp; pop ebp; ret 10h`): nothing stored. Flags after the site are dead.
RS7_STUB(Rs7_Stub_NG1) {
    __asm {
        movzx ecx, byte ptr [ebp+8]
        movzx edx, byte ptr [ebp+0Ch]
        cmp ecx, 8
        jae ng1_reject
        cmp edx, 1Ah
        jae ng1_reject
        imul eax, ecx, 1Ah
        add eax, edx
        add eax, 347h
        jmp g_rs7_resume_NG1
    ng1_reject:
        fstp st(0)
        pop edi
        mov esp, ebp
        pop ebp
        ret 10h
    }
}

// NGP - stack probe for the 1074h-byte frame.
// 0x7E4873 in Rnd_NeckTargets_Update 0x7E4870, the stock `sub esp,0D34h` (6 bytes). The native frame is
// 1074h, more than one page: touch the page between before the final sub so the guard page is committed one
// page at a time (the job of __chkstk, which MSVC would have called for this frame size).
RS7_STUB(Rs7_Stub_NGP) {
    __asm {
        sub esp, 1000h
        test dword ptr [esp], esp
        sub esp, 74h
        jmp g_rs7_resume_NGP
    }
}

// NG9 - string-6 neck targets and tree release.
// 0x7E49D6 in Rnd_NeckTargets_Update 0x7E4870, right after the consumer loop (FPU empty, no XMM in the
// function); replaces the tree-release displacements: `cmp dword [ebp-0Ch],0; pop edi; pop esi; je 0x7E49EA;
// mov edx,[ebp-18h]; push edx; lea ecx,[ebp-1Ch]` (15 bytes; the `call 0x7D06C0` follows). Native frame: grid
// ebp-1060h, ebx = the renderer, dt [ebp+0Ch], t [ebp-106Ch], fret count [ebp-1070h], active string count [ebp-1074h];
// the tree's size / root / header are ebp-34Ch / -358h / -35Ch. Rs7_NeckRow6N (inside pushad / popad) spawns string
// 6 while the active count is 6 (and only reads row 6 when it is empty), Rs7_NeckLog once per song.
RS7_STUB(Rs7_Stub_NG9) {
    __asm {
        pushad
        push dword ptr [ebp-1074h]
        push dword ptr [ebp-1070h]
        push dword ptr [ebp-106Ch]
        push dword ptr [ebp+0Ch]
        push ebx
        lea eax, [ebp-1060h]
        push eax
        call Rs7_NeckRow6N
        add esp, 24
        test eax, eax
        js ng9_out
        push eax
        call Rs7_NeckLog
        add esp, 4
    ng9_out:
        popad
        cmp dword ptr [ebp-34Ch], 0
        pop edi
        pop esi
        je ng9_skip
        mov edx, [ebp-358h]
        push edx
        lea ecx, [ebp-35Ch]
        jmp g_rs7_resume_NG9
    ng9_skip:
        jmp g_rs7_target_NG9
    }
}
