// Naked stubs of the song's 7-string mode (pitch.h): the expected pitch of string 6 (S1); the detector's tuning
// (S3, hooks_mid.cpp; S4, hooks_calls.cpp) are mid-hooks. Exported (with their resume / target variables) so
// tools/tests/test_emu*.py can run them in Unicorn; conventions in docs/ARCHITECTURE.md section 14 "Remaining stubs".
#include "features/pitch/pitch.h"
#include <cstdint>

extern "C" __declspec(dllexport) uintptr_t g_rs7_target_S1 = 0;

// S1 - target of the guitar JA (string > 5) in 0x873F80. EAX = -1, ECX = tuning, ESI = string.
// g_rs7_target_S1 = 0x874020 (add eax,[ebp+8]; pop ebp; ret).
// Stays asm: a rel32 site (the stub is the new target of a branch, not a jump written over
// code), which no mid-hook entry serves; and it is hot (0x873F80 runs per tracked note and frame, PC1 / PC4 call it for
// every string of an untailed object) while its helper Rs7_Base6 is not light-safe (C++ exception handling, the locked
// side store), so a full-mode entry would be over the cost budget and a light one is not allowed.
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Stub_S1() {
    __asm {
        cmp esi, 6
        jne s1_out
        push ecx
        push edx
        push ecx
        call Rs7_Base6
        add esp, 4
        pop edx
        pop ecx
        cmp eax, 80000000h
        jne s1_out
        or eax, -1
    s1_out:
        jmp g_rs7_target_S1
    }
}
