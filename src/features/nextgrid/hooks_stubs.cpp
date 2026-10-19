// Native capacity-8 next-note grid stubs (issue #18 stage 5 area 2, nextgrid.h). Written only with the native
// next-note grid (registration.cpp: kRs7NgNative), a member of the native 7-string unit.
#include "features/nextgrid/nextgrid.h"
#include <cstdint>

// The grid (stock: 6 time rows on the stack of Rnd_NeckTargets_Update 0x7E4870) grows to 8 rows; the in-place byte
// sites NGB* move its offsets (sites.generated.inc), NG1 and NG9 are mid-hooks (hooks_mid.cpp). NGP, the stack probe
// for the frame that grew past one page, stays asm: a probe must touch the stack pages in order before anything is
// written below them, and the mid-hook thunk itself writes below the site's ESP first (docs/ARCHITECTURE.md
// section 15, "stack probes").
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_NGP = 0;

// NGP - stack probe for the 1074h-byte frame.
// 0x7E4873 in Rnd_NeckTargets_Update 0x7E4870, the stock `sub esp,0D34h` (6 bytes). The native frame is
// 1074h, more than one page: touch the page between before the final sub so the guard page is committed one
// page at a time (the job of __chkstk, which MSVC would have called for this frame size).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Stub_NGP() {
    __asm {
        sub esp, 1000h
        test dword ptr [esp], esp
        sub esp, 74h
        jmp g_rs7_resume_NGP
    }
}
