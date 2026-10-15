// Native 8-entry tuning / open-MIDI storage and expected pitch (issue #18 stage 5 area 3,
// core/pitch8.h). Every site here is written only with [neck] native_pitch=1 (registry:
// kRs7PitchNative). Integer only: the game callers can hold live x87 values.
//
// Contents, in file order (the detector / matcher objects grow a tuning8 tail; see core/pitch8.h):
//   PM0 PR0 PT1 PM1    initialise / fill the tails when a matcher or detector is created, reset or given a tuning
//   PE0                port of the expected-pitch function 0x873E90 (eight-entry base table)
//   Rs7_Pitch_Raw8     port of 0x873F80 (shared by PC1-PC4 and the publication stubs)
//   PC1-PC4            callers of 0x873F80: strings >= 6 of a native object read the tail
#include "core/pitch8.h"
#include "hooks/stub_macros.h"
#include <cstdint>

RS7_RESUME_VAR(PM0);
RS7_RESUME_VAR(PR0);
RS7_RESUME_VAR(PM1);
RS7_RESUME_VAR(PE0);
RS7_TARGET_VAR(PC1);
RS7_TARGET_VAR(PC2);
RS7_TARGET_VAR(PC3);
RS7_TARGET_VAR(PC4);

// PM0 - matcher tail initialised when the matcher is created.
// 0x4EADDD `mov dword [eax+40h],-1` in Play_DetectInput_Init: EAX = the new matcher (its stock
// fields set above), ST0 = the fldz stored at 0x4EADE4. Flags are dead (the next instruction is fstp).
RS7_STUB(Rs7_Stub_PM0) {
    __asm {
        mov dword ptr [eax+40h], 0FFFFFFFFh
        pushad
        push eax
        call Rs7Pitch_MatNew
        add esp, 4
        popad
        jmp g_rs7_resume_PM0
    }
}

// PR0 - detector tail initialised on Play_Detector_Reset.
// 0x4DC07B `mov eax,[ebx+11F0h]` in Play_Detector_Reset 0x4DBF20, the join after the guitar /
// bass open-MIDI writers: EBX = detector, tuning[6] zeroed, instrument +11ECh set.
RS7_STUB(Rs7_Stub_PR0) {
    __asm {
        pushad
        push ebx
        call Rs7Pitch_DetReset
        add esp, 4
        popad
        mov eax, dword ptr [ebx+11F0h]
        jmp g_rs7_resume_PR0
    }
}

// PT1 - detector tail after the bass tuning exit of Play_Detector_SetTuning.
// the bass exit of Play_Detector_SetTuning 0x4E2590 (0x4E26BA `mov [eax+1356h],dx; ret`):
// EAX = detector, ESI = std::vector<int16>. ECX/EDX keep the stock values (popad).
RS7_STUB(Rs7_Stub_PT1) {
    __asm {
        mov word ptr [eax+1356h], dx
        pushad
        push esi
        push eax
        call Rs7Pitch_DetTuned
        add esp, 8
        popad
        ret
    }
}

// PM1 - matcher tail after Play_Matcher_SetTuning copies the tuning.
// 0x4E85E5 in Play_Matcher_SetTuning 0x4E85A0, after the six-entry copy loop (the vector has
// >= 6 entries): ESI = matcher, EDX = vector, EBX still pushed. Replays
// `mov eax,[esi+4]; pop ebx; test eax,eax` (the je at the resume reads ZF).
RS7_STUB(Rs7_Stub_PM1) {
    __asm {
        pushad
        push edx
        push esi
        call Rs7Pitch_MatTuned
        add esp, 8
        popad
        mov eax, dword ptr [esi+4]
        pop ebx
        test eax, eax
        jmp g_rs7_resume_PM1
    }
}

// PE0 - native expected-pitch function (port of 0x873E90).
// the port of 0x873E90 (EAX = instrument, EDX = string, stack fret, std::vector<int16>*; plain
// ret, the caller pops 8), entered by the jmp at its entry. The stock six-entry guitar jump table
// becomes an eight-entry base table (35 / 30 for strings 6 / 7); bass and every other string keep
// base -1. The vector bound check stays: a string past the vector goes to the stock failure path
// 0x873F09 (push 119C8A8h; call 433650h) with the stock stack (= g_rs7_resume_PE0 + 73h).
RS7_STUB(Rs7_Stub_PE0) {
    __asm {
        push ebp
        mov ebp, esp
        or ecx, -1
        sub eax, 2
        push esi
        mov esi, dword ptr [ebp+0Ch]
        jz pe_bass
        cmp edx, 7
        ja pe_check
        mov ecx, dword ptr [g_rs7_pitch_gbase + edx*4]
        jmp pe_check
    pe_bass:
        cmp edx, 3
        ja pe_check
        mov ecx, dword ptr [g_rs7_pitch_bbase + edx*4]
    pe_check:
        mov eax, dword ptr [esi]
        mov esi, dword ptr [esi+4]
        sub esi, eax
        sar esi, 1
        cmp edx, esi
        pop esi
        jb pe_ok
        push g_rs7_resume_PE0
        add dword ptr [esp], 73h
        ret
    pe_ok:
        movsx eax, word ptr [eax+edx*2]
        add eax, ecx
        add eax, dword ptr [ebp+8]
        pop ebp
        ret
    }
}

// Rs7_Pitch_Raw8 - the port of 0x873F80 (ECX = int16 tuning*, EDX = instrument, ESI = string, stack fret; plain ret,
// the caller pops 4): null -> 0; guitar strings 0..7 tuning[s] + base8[s] + fret; bass 0..3 likewise;
// anything else fret - 1. Same arithmetic and register effects as the stock cases (EDX = instrument -
// 2, ECX / ESI untouched); the stock jump tables are replaced by base tables, not enlarged.
RS7_STUB(Rs7_Pitch_Raw8) {
    __asm {
        push ebp
        mov ebp, esp
        test ecx, ecx
        jnz r_go
        xor eax, eax
        pop ebp
        ret
    r_go:
        or eax, -1
        sub edx, 2
        jz r_bass
        cmp esi, 7
        ja r_out
        movsx eax, word ptr [ecx+esi*2]
        add eax, dword ptr [g_rs7_pitch_gbase + esi*4]
        add eax, dword ptr [ebp+8]
        pop ebp
        ret
    r_bass:
        cmp esi, 3
        ja r_out
        movsx eax, word ptr [ecx+esi*2]
        add eax, dword ptr [g_rs7_pitch_bbase + esi*4]
    r_out:
        add eax, dword ptr [ebp+8]
        pop ebp
        ret
    }
}

// PC1-PC4 - the calls of 0x873F80 whose tuning pointer is matcher+44h (PC1 0x4E54FB, PC2 0x8742CC,
// PC4 0x8768A6) or detector+1340h (PC3 0x8754AA). Strings 0..5, a null pointer or an object without
// the native tail call the stock function (S1 included) unchanged. Rs7Pitch_Tailed decides (strings
// >= 6 only): _msize first, so nothing past a stock-size block is ever read, then the magic. Strings
// >= 6 of a native object run Rs7_Pitch_Raw8 on its tuning8 tail; EAX/ECX/EDX are kept around the
// check (pops leave ZF) and ECX is restored for the caller. Tuning8: matcher +58h = ECX+14h,
// detector +13B8h = ECX+78h.
//   OBJ_OFF   the stock tuning pointer's offset in its object (ECX - OBJ_OFF = the matcher / detector)
//   DETECTOR  0 = matcher, 1 = detector (Rs7Pitch_Tailed's kind)
//   TAIL_OFF  tuning8 tail offset relative to the stock tuning pointer
#define RS7_PITCH_CALL(ID, OBJ_OFF, DETECTOR, TAIL_OFF)                                                                \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm cmp esi, 6 __asm jb pc_stock __asm test ecx,                                                             \
            ecx __asm jz pc_stock __asm push eax __asm push ecx __asm push edx __asm lea eax,                          \
            [ecx - OBJ_OFF] __asm push DETECTOR __asm push eax __asm call Rs7Pitch_Tailed __asm add esp,               \
            8 __asm test eax, eax __asm pop edx __asm pop ecx __asm pop eax __asm jz pc_stock __asm lea ecx,           \
            [ecx + TAIL_OFF] __asm push dword ptr[esp + 4] __asm call Rs7_Pitch_Raw8 __asm add esp,                    \
            4 __asm lea ecx, [ecx - TAIL_OFF] __asm ret __asm pc_stock                                                 \
            : __asm jmp g_rs7_target_##ID                                                                              \
    }
RS7_PITCH_CALL(PC1, 44h, 0, 14h)
RS7_PITCH_CALL(PC2, 44h, 0, 14h)
RS7_PITCH_CALL(PC3, 1340h, 1, 78h)
RS7_PITCH_CALL(PC4, 44h, 0, 14h)

static_assert(rs7p::kMatTuning8 - 0x44 == 0x14, "PC1/PC2/PC4 offsets");
static_assert(rs7p::kDetTuning8 - 0x1340 == 0x78, "PC3 offsets");
