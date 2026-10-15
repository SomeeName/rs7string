// Native 8-slot tuner stubs ([neck] native_tuner=1, core/tuner8.h, docs/ARCHITECTURE.md section 8). A 7/8-string
// tuner session runs relocated copies of 21 UI_PlayerTuner methods and 3 GuitarStrings readers over its
// capacity-8 tail; these stubs choose the copy or the stock code and own the session's storage:
//   TU8H TU8C TU8A      entry dispatchers of three GuitarStrings readers (copy or original)
//   Rs7_Call_PubEdge    call written into the TU8A copy
//   TU8INIT TU8FREE     session constructor tail / sized deleting destructor (the session is 380h bytes)
//   TS8*                entry dispatchers of the 21 UI_PlayerTuner method copies (ids in tuner8s_ids.generated.h)
// The bytes site TU8ALLOC (no stub) is the allocation that makes the session 380h bytes (stock 278h).
#include "core/publish_grid.h"
#include "core/tuner8.h"
#include "core/tuner8s_ids.generated.h"
#include "hooks/stub_macros.h"

// Entry flags are dead in all three functions. Like the GS entry dispatcher (RS7_ENTRY_DISPATCH,
// hooks/stub_macros.h), preserve every register and the original stack, then RET into the selected
// copy/trampoline. No published count, tuning or slot-0 alias is changed here.
RS7_ENTRY_DISPATCH(TU8H, Rs7Tuner8_Target, 0, eax)  // 5B0770: session in EAX, string on stack (RET 4)
RS7_ENTRY_DISPATCH(TU8C, Rs7Tuner8_Target, 1, esi)  // 5B0810: session in ESI (RET)
RS7_ENTRY_DISPATCH(TU8A, Rs7Tuner8_Target, 2, ecx)  // 7E7140: StringAction in ECX (RET 8)

// Call written into the TU8A copy over PUBEDGE's 13 bytes (7E72D0: EAX = InstrumentInfo, CL = first):
// AL = the edge string; returns to the next copy instruction like the PUBEDGE stub's resume.
RS7_STUB(Rs7_Call_PubEdge) {
    __asm {
        pushad
        movzx ecx, cl
        push ecx
        push eax
        call Rs7Publish_EdgeCopy
        add esp, 8
        mov [esp+28], eax
        popad
        ret
    }
}

RS7_RESUME_VAR(TU8FREE);

// TU8INIT - 5AE305: ctor common return. ESI=owner, entry stack still has saved EBX/EBP.
// x87 is empty here after 5AE2F6. Keep its original return value and cleanup.
RS7_STUB(Rs7_Stub_TU8INIT) {
    __asm {
        pushad
        push esi
        call Rs7Tuner8_InitSession
        add esp,4
        popad
        mov eax,esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}

// TU8FREE - 5AE341: the sized deleting destructor, after the member destructor. Preserve
// allocator EAX/EDX; reserve its size argument exactly where PUSH 278h put it.
RS7_STUB(Rs7_Stub_TU8FREE) {
    __asm {
        sub esp,4
        pushad
        push esi
        call Rs7Tuner8_FreeSessionSize
        add esp,4
        mov [esp+32],eax
        popad
        jmp dword ptr [g_rs7_resume_TU8FREE]
    }
}

// Native tuner session copies (tools/gen_tuner8s.py): every register and the stack as at the entry,
// then RET into the copy (a native session) or the trampoline (stock bytes + jmp back).
#define RS7_TS8_STUB(ID, IDX) RS7_ENTRY_DISPATCH(ID, Rs7Tuner8_SessTarget, IDX, esp)
RS7_TU8S_SPECS(RS7_TS8_STUB)
#undef RS7_TS8_STUB
