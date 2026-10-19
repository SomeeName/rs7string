// The headstock feature's naked stubs and its link variables (exported, so the emulator tests in tools/tests run them
// and Rs7_Startup binds the mid-hooks' resume / target). The other PS* sites, LT1 and AL1 are C++ mid-hooks
// (hooks_mid.cpp), PSN2 a Wrap mid-hook (hooks_wrap.cpp). What stays asm, and why:
//   Rs7_NifHandlerStock  the callable stock entry of the nif handler for Rs7_NifHandler (AL1's displaced bytes + a jump
//                        back): a trampoline C code calls, not a site
#include "features/headstock/assetload.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegset.h"
#include "features/string7/string7.h"
#include <cstdint>

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSL1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PSL2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSA2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSD1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSD2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSU1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PSU1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSU2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PSU2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSU3 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSM1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PSM1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSM2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PSM2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSR1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_PSN1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_LT1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_AL1 = 0;

// ---- native N-slot PegSet (pegset.h, issue #18 stage 3) ----

// The 8-slot capacity of PSA2 / PSD1 / PSD2 / PSU1-PSU3 / PSM1 / PSM2 / PSL1 applies only to a native
// PegSet: one registered in g_rs7_pegsets by Rs7_PegSetCtor (PSC1). A PegSet constructed before the
// patch (the boot's first guitar) has the stock 0x15C layout: its pegColor[6] at +0FCh is where slots
// 6/7 would be, so an 8-slot unload read a colour float as an entity (measured 2026-10-13: AV at
// 0x59C3E7, ECX 3F800000, title -> main menu). Those keep every stock count. The decision never reads
// the PegSet (review #1: the old magic check at +1D0h read 74h bytes past a stock 0x15C allocation).

// PSA2 (0x58DA05, the PegSet's sized free) uses a C++ mid-hook callback (hooks_mid.cpp).

// PSD1 (0x59C30D) and PSD2 (0x59C314), the PegSet destructor's release counter and anchor, use C++ mid-hook
// callbacks (hooks_mid.cpp).

// PSU3 (0x59C41D, the unload's taken-flag clear count) uses a C++ mid-hook callback (hooks_mid.cpp).

// PSU1 / PSU2 / PSM1 (the unload and mesh collection slot bounds), PSM2 (0x541D34, the peg mesh collection pass latch)
// and PSL2 (the load-complete call, After placement) use C++ mid-hook callbacks (hooks_mid.cpp).

// PSC1 (0x59C2F4, the PegSet constructor tail), PSL1 (0x59C4D0, the load count) and PSL2 (0x59C60C, the load complete)
// use C++ mid-hook callbacks (hooks_mid.cpp).

// PSR1 (0x5B0A21, the colour reset loop bound) and PSN1 (0x5B089D, the colour name lookup) use C++ mid-hook
// callbacks (hooks_mid.cpp).

// The stock nif handler for Rs7_NifHandler: the same replay of AL1's 8 bytes, then 0x4A8568.
extern "C" __declspec(dllexport) __declspec(naked) void* __cdecl Rs7_NifHandlerStock(const char*, const void*, uint32_t,
                                                                                     void*) {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, dword ptr [g_rs7_frame_global]
        mov eax, dword ptr [eax]
        jmp g_rs7_resume_AL1
    }
}
