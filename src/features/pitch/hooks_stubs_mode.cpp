// Naked stubs of the song's 7-string mode (pitch.h): the expected pitch of string 6 (S1, S2); the detector's tuning
// (S3, hooks_mid.cpp; S4, hooks_calls.cpp) are mid-hooks. Exported (with their resume / target variables) so
// tools/tests/test_emu*.py can run them in Unicorn; conventions in docs/ARCHITECTURE.md section 15 "Remaining stubs".
#include "features/pitch/pitch.h"
#include <cstdint>

extern "C" __declspec(dllexport) uintptr_t g_rs7_target_S1 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_S2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_S2 = 0;

// S1 - target of the guitar JA (string > 5) in 0x873F80. EAX = -1, ECX = tuning, ESI = string.
// g_rs7_target_S1 = 0x874020 (add eax,[ebp+8]; pop ebp; ret).
// Stays asm (asm reduction part 3): a rel32 site (the stub is the new target of a branch, not a jump written over
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

// S2 - replaces `cmp edx,5; ja` in 0x873E90. EDX = string, ESI = std::vector<int16>* tuning,
// ECX = -1, [ebp+8] = fret. g_rs7_resume_S2 = the jump-table dispatch right after the site,
// g_rs7_target_S2 = 0x873EFB (bounds check + tuning[string] + fret).
// Stays asm: over the full-mode cost budget (ARCHITECTURE.md section 15). Play_NoteJudge_DetectNotes 0x7B60D0 runs
// it every frame for each unjudged note of its +-0.5 s window (dozens in dense passages), besides once per note built;
// light mode is not possible, Rs7_NoteString6 logs (CRT formatting). Only reached without the native pitch storage:
// PE0 answers 0x873E90 at its entry (its failure path continues at 0x873F09, past this site), so S2 runs only when the
// Pitch group is off (no MSVCR100 _msize). Kept as the documented legacy fallback (asm reduction part 3).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Stub_S2() {
    __asm {
        cmp edx, 5
        ja s2_hi
        jmp g_rs7_resume_S2
    s2_hi:
        cmp edx, 6
        jne s2_out
        mov ecx, [esi+4]
        sub ecx, [esi]
        cmp ecx, 14
        jne s2_stock
        pushad
        mov eax, [esi]
        movsx eax, word ptr [eax+12]
        push eax
        push dword ptr [ebp+8]
        call Rs7_NoteString6
        add esp, 8
        popad
        mov ecx, 35
        jmp g_rs7_target_S2
    s2_stock:
        or ecx, -1
    s2_out:
        jmp g_rs7_target_S2
    }
}

// I1 (the inverse pitch of string 6) is a C++ mid-hook (hooks_mid.cpp).

// S3 (the guitar exit of Play_Detector_SetTuning) is a C++ mid-hook (hooks_mid.cpp).
