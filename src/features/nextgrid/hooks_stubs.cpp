// Native capacity-8 next-note grid stubs (nextgrid.h). Written only with the native
// next-note grid (registration.cpp: kRsxrNgNative), a member of the native 7-string unit.
#include "features/nextgrid/nextgrid.h"
#include <cstdint>

// The grid (stock: 6 time rows on the stack of Rnd_NeckTargets_Update 0x7E4870) grows to 8 rows; the in-place byte
// sites NGB* move its offsets (sites.generated.inc), NG1 and NG9 are mid-hooks (hooks_mid.cpp). NGP, the stack probe
// for the frame that grew past one page, stays asm: a probe must touch the stack pages in order before anything is
// written below them, and the mid-hook thunk itself writes below the site's ESP first (docs/ARCHITECTURE.md
// section 14, "stack probes").
extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_NGP = 0;
// The /GS cookie the displaced `mov eax,[__security_cookie]` reads (its address is part of NGP's expected bytes
// `A1 40 B2 25 01`, so the site does not resolve on a build where it differs).
extern "C" __declspec(dllexport) const uintptr_t g_rsxr_cookie_NGP = 0x125B240;

// NGP - stack probe for the 1074h-byte frame.
// 0x7E4879 in Rnd_NeckTargets_Update 0x7E4870, `mov eax,[__security_cookie]` (5 bytes) right after the stock
// prologue `push ebp / mov ebp,esp / sub esp,0D34h`. The prologue stays stock: RSMods (xinput1_3) detours those
// 9 bytes, and a jmp of ours inside them was either overwritten or copied unrelocated into its trampoline (a jump
// into nowhere, crash 2026-10-29). Here the frame grows by 340h to 1074h, more than one page: the pages below
// ebp are touched top-down so the guard page is committed one page at a time (the job of __chkstk). The function
// ends with `mov esp,ebp`, so the extra space needs no epilogue change.
extern "C" __declspec(dllexport) __declspec(naked) void Rsxr_Stub_NGP() {
    __asm {
        sub esp, 340h
        test dword ptr [ebp - 800h], esp
        test dword ptr [ebp - 1000h], esp
        test dword ptr [esp], esp
        mov eax, g_rsxr_cookie_NGP  // the displaced `mov eax,[__security_cookie]`, through a register (no ds: operand)
        mov eax, dword ptr [eax]
        jmp g_rsxr_resume_NGP
    }
}
