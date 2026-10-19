// Naked stubs of the native N-slot chord tracking (nslot.h, tracker.h): the release and aggregation of the child evals
// of strings 6/7. Exported for tools/tests/test_emu.py; conventions in docs/ARCHITECTURE.md section 15 "Remaining
// stubs". CC3, CH3, AG1-AG3 (hooks_eval.cpp) and the technique-check latches UP1-UP3, PD2-PD5 (hooks_latches.cpp) are
// C++ mid-hooks. What stays asm, and why:
//   RL1 RL2       call the game's eval release (g_rs7_fn_evrel, ECX = tracker, stdcall slot) from the tracker's live
//                 state and leave its EAX/ECX/EDX: a light callback may not call unverified game code, and full mode
//                 would run it on a reset x87 state
#include "features/nslot/nslot.h"
#include <cstdint>

// ---- native N-slot chord tracking (tracker.h): child evals for strings 6/7 at eval +0C1Eh

// RL1 / RL2 - release the child evals of strings 6/7 with their parent.
// 0x87467B / 0x87470D in the eval releases 0x874610 / 0x8746D0 (ESI = tracker + slot *
// 0C28h, the parent eval at ESI + 8; EDI = tracker; inside `if isChord`): first the children of strings
// 6/7 (bytes at ESI + 0C26h/0C27h) are released like the stock six (0x8746D0, stdcall slot, ECX =
// tracker) and set to FFh, then the replay `add esi,0C0Ch` and the stock loop over subEval[6].
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_RL1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_RL2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_evrel = 0;
#define RS7_NS_RELEASE(name, resume)                                                                                   \
    extern "C" __declspec(dllexport) __declspec(naked) void name() {                                                   \
        __asm movzx eax, byte ptr[esi + 0C26h] __asm cmp al, 0FFh __asm je rel_s7 __asm push eax __asm mov ecx,        \
            edi __asm call g_rs7_fn_evrel __asm mov byte ptr[esi + 0C26h], 0FFh __asm rel_s7                           \
            : __asm movzx eax, byte ptr[esi + 0C27h] __asm cmp al,                                                     \
              0FFh __asm je rel_done __asm push eax __asm mov ecx,                                                     \
              edi __asm call g_rs7_fn_evrel __asm mov byte ptr[esi + 0C27h], 0FFh __asm rel_done                       \
            : __asm add esi, 0C0Ch __asm jmp resume                                                                    \
    }
RS7_NS_RELEASE(Rs7_Stub_RL1, g_rs7_resume_RL1)
RS7_NS_RELEASE(Rs7_Stub_RL2, g_rs7_resume_RL2)
