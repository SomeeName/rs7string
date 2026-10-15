// Native capacity-8 FretGrid stub (issue #18 stage 5 area 1, core/fretgrid.h). Written only with
// [neck] native_fretgrid=1 (registry: kRs7FgNative).
#include "core/fretgrid.h"
#include "hooks/stub_macros.h"
#include <cstdint>

RS7_RESUME_VAR(FGRB);

// FGRB - local points of the extra FretGrid cells 6/7.
// 0x58FA25 `mov eax, 2` in Rnd_FretGrid_Build 0x58F8A0 (the "complete" exit; EBP frame:
// [ebp+8] = FretGrid, [ebp+0Ch] = guitar; x87 empty, flags dead): build the local points of cells 6/7,
// then replay the instruction.
RS7_STUB(Rs7_Stub_FGRB) {
    __asm {
        pushad
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+8]
        call Rs7Fg_BuildExtra
        add esp, 8
        popad
        mov eax, 2
        jmp g_rs7_resume_FGRB
    }
}
