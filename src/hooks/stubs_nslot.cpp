// Naked stubs of the native N-slot chord data (core/nslot.h, issue #18 stage 2) and the chord code that
// reads it. Exported for tools/tests/test_emu.py; conventions in hooks/stub_macros.h.
//
// Contents, in file order (sites in sites.json; the byte-patch sites NR*, NB*, NS*, CT*, SH*, BH1, HS*,
// NN*, NK1, NM*, FG*, SM*, DM1 need no stub):
//   Rs7_NsTplAt / Rs7_NsCnAt  helpers: the song's T8 template / C8 ChordNotes record for an id
//   TP1-TP8 TP11 TP12 CP1 CP2 template / ChordNotes stride sites: `begin + id*48h` / `id*948h` -> T8 / C8
//   PP1 SF1                   song load / free: build and drop the T8 / C8 arrays
//   TM1-TM6 DS1               string-mask test and double-stop count over the 8-slot records
//   MD1 LU3 AM1               chord detection: matcher entry, Lua binding, mistake analysis
//   WC2 EV1-EV5 BT1 CS1       tracker evals: int32[8] target arrays, tails built / cleared
//   PL1                       chord pluck of string 6
//   CC3 CH3 RL1 RL2 AG1-AG3   child evals for strings 6/7 (eval +0C1Eh), their release and aggregation
//   UP1-UP3 PD2-PD5           per-string technique checks (loop latches): strings 6/7 from the tails
#include "core/nslot.h"
#include "hooks/stub_macros.h"
#include <cstdint>

// Template / ChordNotes pointer redirects: every stride site that turns a template id into
// `begin + id * 48h` (a ChordNotes id into `begin + id * 948h`) gets the song's superset record
// instead (T8 / C8). Rs7_NsTplAt / Rs7_NsCnAt: EAX = the stock vector begin, EDX = the id -> EAX = the
// superset record, or the stock record when the vector is not registered (a song still loading, not an
// SNG song, an id out of range). They keep every register but EAX and the flags (dead at every site:
// the next flag reader follows a flag-setting instruction) and touch no FPU/SSE state. Each stub
// replays the other side effects of the replaced instructions (the scratch register = id * 9 /
// id * 948h).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_NsTplAt() {
    __asm {
        push ecx
        push esi
        mov esi, offset g_rs7_ns
        mov ecx, 64  // kRs7NsEntries
    tpl_next:
        cmp [esi], eax
        jne tpl_skip
        cmp edx, [esi+4]
        jae tpl_skip
        mov eax, edx
        shl eax, 7
        add eax, [esi+8]
        pop esi
        pop ecx
        ret
    tpl_skip:
        add esi, 32
        dec ecx
        jnz tpl_next
        lea ecx, [edx+edx*8]
        lea eax, [eax+ecx*8]
        pop esi
        pop ecx
        ret
    }
}

extern "C" __declspec(dllexport) __declspec(naked) void Rs7_NsCnAt() {
    __asm {
        push ecx
        push esi
        mov esi, offset g_rs7_ns
        mov ecx, 64  // kRs7NsEntries
    cn_next:
        cmp [esi+0Ch], eax
        jne cn_skip
        cmp edx, [esi+10h]
        jae cn_skip
        imul eax, edx, 15A8h
        add eax, [esi+14h]
        pop esi
        pop ecx
        ret
    cn_skip:
        add esi, 32
        dec ecx
        jnz cn_next
        imul ecx, edx, 948h
        add eax, ecx
        pop esi
        pop ecx
        ret
    }
}

RS7_RESUME_VAR(TP1);
RS7_RESUME_VAR(TP2);
RS7_RESUME_VAR(TP3);
RS7_RESUME_VAR(TP4);
RS7_RESUME_VAR(TP5);
RS7_RESUME_VAR(TP6);
RS7_RESUME_VAR(TP7);
RS7_RESUME_VAR(TP8);
RS7_RESUME_VAR(TP11);
RS7_RESUME_VAR(CP1);
RS7_RESUME_VAR(CP2);

// TP1 - T8 template for SNG_Song_GetChord.
// 0x558A2E SNG_Song_GetChord 0x558A00 (EDI id, ESI begin): `lea ecx,[edi+edi*8]; lea eax,[esi+ecx*8]`
RS7_STUB(Rs7_Stub_TP1) {
    __asm {
        push edx
        mov eax, esi
        mov edx, edi
        call Rs7_NsTplAt
        pop edx
        lea ecx, [edi+edi*8]
        jmp g_rs7_resume_TP1
    }
}

// TP2 - T8 template in the section dimming (Rnd_GuitarStrings_UpdateSectionDimming).
// 0x4608AC Rnd_GuitarStrings_UpdateSectionDimming 0x460550: `lea edx,[edi+edi*8]; lea edi,[ebx+edx*8]`
RS7_STUB(Rs7_Stub_TP2) {
    __asm {
        push eax
        mov eax, ebx
        mov edx, edi
        call Rs7_NsTplAt
        lea edx, [edi+edi*8]
        mov edi, eax
        pop eax
        jmp g_rs7_resume_TP2
    }
}

// TP3 - T8 template in the finger keep table.
// 0x58C38D finger keep table 0x58C300: `lea ecx,[ebx+ebx*8]; lea ebx,[edi+ecx*8]`
RS7_STUB(Rs7_Stub_TP3) {
    __asm {
        push eax
        push edx
        mov eax, edi
        mov edx, ebx
        call Rs7_NsTplAt
        lea ecx, [ebx+ebx*8]
        mov ebx, eax
        pop edx
        pop eax
        jmp g_rs7_resume_TP3
    }
}

// TP4 / TP6 / TP8 - T8 template in the finger display, chord box and chord spawn.
// 0x58CCD7 finger display 0x58CC80, 0x7A003F in 0x79FF30, 0x7E64E1 chord spawn
// 0x7E6400: `lea ecx,[edi+edi*8]; lea edx,[ebx+ecx*8]`
#define RS7_TP_EDI_EBX_EDX(name, resume)                                                                               \
    RS7_STUB(name) {                                                                                                   \
        __asm push eax __asm mov eax, ebx __asm mov edx, edi __asm call Rs7_NsTplAt __asm mov edx,                     \
            eax __asm lea ecx, [edi + edi * 8] __asm pop eax __asm jmp resume                                          \
    }
RS7_TP_EDI_EBX_EDX(Rs7_Stub_TP4, g_rs7_resume_TP4)
RS7_TP_EDI_EBX_EDX(Rs7_Stub_TP6, g_rs7_resume_TP6)
RS7_TP_EDI_EBX_EDX(Rs7_Stub_TP8, g_rs7_resume_TP8)

// TP5 - T8 template in the chord render at 0x79CDF0.
// 0x79CFAE in 0x79CDF0: `lea edx,[edi+edi*8]; lea edi,[ecx+edx*8]` (x87 stack not empty here:
// the helper does not touch it)
RS7_STUB(Rs7_Stub_TP5) {
    __asm {
        push eax
        mov eax, ecx
        mov edx, edi
        call Rs7_NsTplAt
        lea edx, [edi+edi*8]
        mov edi, eax
        pop eax
        jmp g_rs7_resume_TP5
    }
}

// TP7 - T8 template in the chord name.
// 0x7A150B chord name 0x7A14A0: `lea ecx,[esi+esi*8]; xor esi,esi; lea ebx,[ebx+ecx*8]`
RS7_STUB(Rs7_Stub_TP7) {
    __asm {
        push eax
        push edx
        mov eax, ebx
        mov edx, esi
        call Rs7_NsTplAt
        mov ebx, eax
        lea ecx, [esi+esi*8]
        pop edx
        pop eax
        xor esi, esi
        jmp g_rs7_resume_TP7
    }
}

// TP11 - T8 template in the section string mask.
// 0x55CC1C SNG_Song_PostProcessNotes (section string mask): `lea ecx,[ecx+ecx*8]; lea edx,[esi+ecx*8]`.
// (TP9/TP10 were dropped: those loops read frets the capo rule 0x55B4A2 had just rewritten on the
// stock vector, and stay six-wide.)
RS7_STUB(Rs7_Stub_TP11) {
    __asm {
        push eax
        mov eax, esi
        mov edx, ecx
        call Rs7_NsTplAt
        lea ecx, [ecx+ecx*8]
        mov edx, eax
        pop eax
        jmp g_rs7_resume_TP11
    }
}

// CP1 - C8 ChordNotes record for SNG_Song_GetChordNotes.
// 0x558A70 SNG_Song_GetChordNotes 0x558A40 (EAX id, ESI begin): `imul eax,eax,948h; add eax,esi`
RS7_STUB(Rs7_Stub_CP1) {
    __asm {
        push edx
        mov edx, eax
        mov eax, esi
        call Rs7_NsCnAt
        pop edx
        jmp g_rs7_resume_CP1
    }
}

// CP2 - C8 ChordNotes record in the chord spawn.
// 0x7E6553 chord spawn 0x7E6400 (ESI id, EBX begin): `imul esi,esi,948h; add ebx,esi`
RS7_STUB(Rs7_Stub_CP2) {
    __asm {
        push eax
        push edx
        mov eax, ebx
        mov edx, esi
        call Rs7_NsCnAt
        mov ebx, eax
        imul esi, esi, 948h
        pop edx
        pop eax
        jmp g_rs7_resume_CP2
    }
}

// PP1 - build the T8 / C8 arrays after SNG_Song_PostProcess.
// the call of SNG_Song_PostProcess 0x559FD0 (two stack arguments, `ret 8`) at 0x569137 (loader
// thread): the stock call, then Rs7Ns_OnPostProcessed(song = argument 1) re-syncs the T8 array and
// builds the C8 array. The caller does not use the return value (0x56913C reloads EAX).
RS7_TARGET_VAR(PP1);
RS7_STUB(Rs7_Stub_PP1) {
    __asm {
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        call g_rs7_target_PP1
    }

    RS7_SAVE_ALL
    __asm {
        push dword ptr [esi+28h]  // pushad (20h) + pushfd (4) + the return address (4): argument 1
        call Rs7Ns_OnPostProcessed
        add esp, 4
    }
    RS7_RESTORE_ALL
    __asm ret 8
}

// SF1 - drop the T8 / C8 arrays when the song is freed.
// 0x558686 in the SNG_Song scalar deleting destructor 0x558680 (ESI = this): the call of the
// destructor body 0x5586C0. Rs7Ns_OnSongFree(this) drops the song's T8/C8 arrays first.
RS7_TARGET_VAR(SF1);
RS7_STUB(Rs7_Stub_SF1) {
    RS7_SAVE_ALL
    __asm {
        push dword ptr [esi+4]  // the saved ESI in the pushad frame (EDI at +0, ESI at +4)
        call Rs7Ns_OnSongFree
        add esp, 4
    }
    RS7_RESTORE_ALL
    __asm jmp g_rs7_target_SF1
}

// ---- native N-slot chord rendering (in-place byte sites NR*, NB*, NS*, CT*, SH*, BH1, HS*, NN*, NK1,
// NM*, FG*, SM*, DM1 need no stub)

// TM1-TM6 - string-mask test over the eight-slot C8 mask.
// the calls of SNG_ChordNotes_TestStringMask 0x5581C0 in the neck-target code (0x7A56F0,
// 0x7A5D50, 0x7A6A50): ECX = ChordNotes record (a C8 at play time, the callers test it for NULL),
// AL = string, [esp+4] = mask flag, `ret 4`; AL = (string < 8 && C8 mask[8][string] & flag), as stock
// computes it over mask[6] (EAX = 0/1, EDX clobbered).
#define RS7_NS_TESTMASK(name)                                                                                          \
    RS7_STUB(name){                                                                                                    \
        __asm cmp al,                                                                                                  \
        8 __asm jb tm_in __asm xor al,                                                                                 \
        al __asm ret 4 __asm tm_in : __asm movzx eax,                                                                  \
        al __asm mov edx,                                                                                              \
        [ecx + eax * 4 + 948h] __asm and edx,                                                                          \
        [esp + 4] __asm xor eax,                                                                                       \
        eax __asm cmp eax,                                                                                             \
        edx __asm sbb eax,                                                                                             \
        eax __asm neg eax __asm ret 4                                                                                  \
    }
RS7_TARGET_VAR(TM1);
RS7_TARGET_VAR(TM2);
RS7_TARGET_VAR(TM3);
RS7_TARGET_VAR(TM4);
RS7_TARGET_VAR(TM5);
RS7_TARGET_VAR(TM6);
RS7_NS_TESTMASK(Rs7_Stub_TM1)
RS7_NS_TESTMASK(Rs7_Stub_TM2)
RS7_NS_TESTMASK(Rs7_Stub_TM3)
RS7_NS_TESTMASK(Rs7_Stub_TM4)
RS7_NS_TESTMASK(Rs7_Stub_TM5)
RS7_NS_TESTMASK(Rs7_Stub_TM6)

// DS1 - double-stop count over the T8 frets.
// 0x55B9ED in SNG_Song_PostProcessNotes (loader thread; ECX = chord id, ESI = template vector
// begin): `lea ecx,[ecx+ecx*8]; xor edx,edx; lea eax,[esi+ecx*8+4]; lea ecx,[edx+6]` sets up the
// double-stop count (frets != -1 over the template's six frets; the note gets 0x02000000 when it is
// 2). Here: EAX = T8 frets[8] of that template, ECX = 7 strings, EDX = 0. Flags dead (the loop's
// `cmp byte [eax],0FFh` follows).
RS7_RESUME_VAR(DS1);
RS7_STUB(Rs7_Stub_DS1) {
    __asm {
        mov eax, esi
        mov edx, ecx
        call Rs7_NsTplAt
        add eax, 48h
        xor edx, edx
        mov ecx, 7
        jmp g_rs7_resume_DS1
    }
}

// ---- native N-slot chord detection (core/nsmatch.h, core/nslot.h)

// MD1 - chord detection over eight targets (entry of Play_Matcher_DetectChord).
// entry of Play_Matcher_DetectChord 0x4E7B30 (stdcall: [esp+4] matcher, [esp+8] int32 targets[8],
// [esp+0Ch] tolerance, [esp+10h] history index; AL = matched; `ret 10h`). Rs7Ns_DetectChord compacts
// up to six targets into a local int32[6] (returns -1: the stock body runs on it, prologue replayed,
// continuing at 0x4E7B39) or judges seven/eight targets itself (returns 0/1). Callee-saved registers
// survive both ways; ECX/EDX are scratch as in stock; the x87 stack is empty at the call (stdcall).
RS7_RESUME_VAR(MD1);
extern "C" int __cdecl Rs7Ns_DetectChord(const uint8_t*, const int32_t*, int, int, int32_t*);
RS7_STUB(Rs7_Stub_MD1) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 32
        push ebx
        push esi
        push edi
        lea eax, [ebp-20h]
        push eax
        push dword ptr [ebp+14h]
        push dword ptr [ebp+10h]
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+8]
        call Rs7Ns_DetectChord
        add esp, 20
        pop edi
        pop esi
        pop ebx
        cmp eax, -1
        jne md_done
        push dword ptr [ebp+14h]
        push dword ptr [ebp+10h]
        lea eax, [ebp-20h]
        push eax
        push dword ptr [ebp+8]
        call md_stock
    md_done:
        mov esp, ebp
        pop ebp
        ret 10h
    md_stock:
        push ebp
        mov ebp, esp
        sub esp, 0A64h
        jmp g_rs7_resume_MD1
    }
}

// LU3 - Lua IsChordBeingPlayed passes int32[8].
// 0x40213B in the Lua binding IsChordBeingPlayed 0x402100 (its six MIDI arguments are stored
// to EBP-20h..-0Ch; LU1/LU2 moved the /GS cookie from EBP-8 to the free EBP-24h): replay
// `mov [ebp-10h],ecx; mov ecx,[ebp+8]`, then targets 6 and 7 (EBP-8, EBP-4) = -1, so 0x4E9470 gets
// an int32[8].
RS7_RESUME_VAR(LU3);
RS7_STUB(Rs7_Stub_LU3) {
    __asm {
        mov [ebp-10h], ecx
        mov ecx, [ebp+8]
        mov dword ptr [ebp-8], -1
        mov dword ptr [ebp-4], -1
        jmp g_rs7_resume_LU3
    }
}

// AM1 - chord-mistake analysis with eight shifted targets.
// 0x87567D in Play_AnalyzeChordMistake 0x8755C0: the call of 0x4E6A70 (stdcall targets, tolerance,
// two time floats; ESI = matcher) with the chord's six targets shifted by EDI semitones (EBP-1Ch;
// [EBP-44h] = the template, a T8). The call gets an int32[8] on the stub's stack: those six plus the
// T8's strings 6/7 shifted the same way (Rs7Ns_Shift8, integer only).
RS7_TARGET_VAR(AM1);
extern "C" void __cdecl Rs7Ns_Shift8(int32_t*, const int32_t*, const uint8_t*, int);
RS7_STUB(Rs7_Stub_AM1) {
    __asm {
        sub esp, 32
        pushad
        lea eax, [esp+32]
        push edi
        push dword ptr [ebp-44h]
        push dword ptr [esp+76]
        push eax
        call Rs7Ns_Shift8
        add esp, 16
        popad
        push dword ptr [esp+48]
        push dword ptr [esp+48]
        push dword ptr [esp+48]
        lea eax, [esp+12]
        push eax
        call g_rs7_target_AM1
        add esp, 32
        ret 10h
    }
}

// WC2 - whole-chord tracker update reads the eval's int32[8].
// 0x87632F in the tracker's whole-chord update 0x8762D0 (EDI = the eval; WC1 grew the frame by
// 20h): instead of the stock six targets at EBP-20h the matcher calls get eval+284h's int32[8] (the
// template copy's midi[6] + the N-slot tails) copied to EBP-44h (WC3-WC7 repoint the LEAs). Replays
// `lea esi,[...]; mov eax,ebx` with ESI = the new array; the two stores of the stock copy it replaces
// ([ebp-10h], [ebp-0Ch]) belong to the old array. Continues at the BassAdj call 0x87633A.
RS7_RESUME_VAR(WC2);
RS7_STUB(Rs7_Stub_WC2) {
    __asm {
        push ecx
        mov ecx, [edi+284h]
        mov [ebp-44h], ecx
        mov ecx, [edi+288h]
        mov [ebp-40h], ecx
        mov ecx, [edi+28Ch]
        mov [ebp-3Ch], ecx
        mov ecx, [edi+290h]
        mov [ebp-38h], ecx
        mov ecx, [edi+294h]
        mov [ebp-34h], ecx
        mov ecx, [edi+298h]
        mov [ebp-30h], ecx
        mov ecx, [edi+29Ch]
        mov [ebp-2Ch], ecx
        mov ecx, [edi+2A0h]
        mov [ebp-28h], ecx
        pop ecx
        lea esi, [ebp-44h]
        mov eax, ebx
        jmp g_rs7_resume_WC2
    }
}

// EV1-EV5 - eval target arrays: eval+2Ch (six) becomes eval+284h (eight).
// calls of the chord matcher wrappers with an eval's six targets eval+2Ch as the first stack
// argument (0x876A40 / 0x87762F -> 0x4E6630, 0x877680 -> 0x4E6680, 0x876A8A / 0x877730 -> 0x4E6A70):
// the argument becomes eval+284h, the eval's int32[8] (2Ch + 258h).
RS7_TARGET_VAR(EV1);
RS7_TARGET_VAR(EV2);
RS7_TARGET_VAR(EV3);
RS7_TARGET_VAR(EV4);
RS7_TARGET_VAR(EV5);
#define RS7_NS_EVAL8(name, target)                                                                                     \
    RS7_STUB(name) {                                                                                                   \
        __asm add dword ptr[esp + 4], 258h __asm jmp target                                                            \
    }
RS7_NS_EVAL8(Rs7_Stub_EV1, g_rs7_target_EV1)
RS7_NS_EVAL8(Rs7_Stub_EV2, g_rs7_target_EV2)
RS7_NS_EVAL8(Rs7_Stub_EV3, g_rs7_target_EV3)
RS7_NS_EVAL8(Rs7_Stub_EV4, g_rs7_target_EV4)
RS7_NS_EVAL8(Rs7_Stub_EV5, g_rs7_target_EV5)

// BT1 - write the eval's N-slot tails (epilogue of Play_Eval_Build).
// 0x874340, the epilogue of Play_Eval_Build 0x874180 (`pop edi; pop esi; pop ebx; pop ebp;
// ret 14h`; EBX = EAX = the eval, [EBP+14h] = template, [EBP+18h] = ChordNotes, either may be NULL):
// Rs7Ns_OnEvalBuilt writes the eval's N-slot tails, then the epilogue runs here.
extern "C" void __cdecl Rs7Ns_OnEvalBuilt(uint8_t*, const uint8_t*, const uint8_t*);
RS7_STUB(Rs7_Stub_BT1) {
    __asm {
        pushad
        push dword ptr [ebp+18h]
        push dword ptr [ebp+14h]
        push ebx
        call Rs7Ns_OnEvalBuilt
        add esp, 12
        popad
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 14h
    }
}

// CS1 - empty N-slot tails in Play_Eval_ClearState.
// 0x87443D in Play_Eval_ClearState 0x874350 (EAX = eval, ECX = 0, EDX = -1): replay
// `mov [eax+0C1Ch],cx`, then the empty N-slot tails (midi[6..7] = -1, masks 0, frets/fingers/slides
// = -1, vibrato 0) and no child evals for strings 6/7 (+0C1Eh = FFFFh). Flags unchanged.
RS7_RESUME_VAR(CS1);
RS7_STUB(Rs7_Stub_CS1) {
    __asm {
        mov [eax+0C1Ch], cx
        mov word ptr [eax+0C1Eh], 0FFFFh
        mov [eax+29Ch], edx
        mov [eax+2A0h], edx
        mov [eax+2A4h], ecx
        mov [eax+2A8h], ecx
        mov [eax+2ACh], edx
        mov [eax+2B0h], edx
        mov [eax+2B4h], ecx
        mov [eax+2B8h], ecx
        jmp g_rs7_resume_CS1
    }
}

RS7_RESUME_VAR(BT1);  // not used: BT1 ends Play_Eval_Build itself

// ---- chord pluck (string vibration on a chord hit): 0x7E2DE0

// TP12 - T8 template in the chord pluck.
// 0x7E2E80 in the chord pluck 0x7E2DE0 (EBX = chord id, EDI = template vector begin):
// `lea edx,[ebx+ebx*8]; lea edi,[edi+edx*8+4]` -> EDI = the T8's frets (+4), EDX = id * 9.
RS7_RESUME_VAR(TP12);
RS7_STUB(Rs7_Stub_TP12) {
    __asm {
        push eax
        mov eax, edi
        mov edx, ebx
        call Rs7_NsTplAt
        lea edi, [eax+4]
        lea edx, [ebx+ebx*8]
        pop eax
        jmp g_rs7_resume_TP12
    }
}

// PL1 - pluck string 6 of a chord.
// 0x7E2EFE in 0x7E2DE0, right after its six-string pluck loop (EDI = T8 + 0Ah, past frets[5];
// [EBP+8] = play session): string 6 of the chord (T8 frets[6] = [EDI+44h]) is plucked like a single
// string-6 note through Rs7_S7Pluck (SNG fret, display fret 1 for an open string, GuitarStrings =
// session+37Ch -> +0Ch), which runs the native pluck copy on a native GuitarStrings (stock rejects
// string 6: nothing happens otherwise). Then the replay `mov al,[ebp-10h]; cmp al,0FFh`.
RS7_RESUME_VAR(PL1);
extern "C" int __cdecl Rs7_S7Pluck(void* gs, uint32_t sngFret, uint32_t fret);
RS7_STUB(Rs7_Stub_PL1) {
    __asm {
        movzx eax, byte ptr [edi+44h]
        cmp al, 0FFh
        je pl_done
        pushad
        movzx edx, al
        test al, al
        jnz pl_fretted
        mov al, 1
    pl_fretted:
        movzx eax, al
        push eax
        push edx
        mov ecx, [ebp+8]
        mov ecx, [ecx+37Ch]
        push dword ptr [ecx+0Ch]
        call Rs7_S7Pluck
        add esp, 12
        popad
    pl_done:
        mov al, [ebp-10h]
        cmp al, 0FFh
        jmp g_rs7_resume_PL1
    }
}

// ---- native N-slot chord tracking (core/nstracker.h): child evals for strings 6/7 at eval +0C1Eh

// CC3 - chord string -> note conversion reads the C8 mask.
// 0x87488E in the chord-string-to-note conversion 0x8747F0 (EDX = ChordNotes, a C8; ECX =
// string): `test dword [edx+ecx*4],1000h` reads C8 mask[8] (+948h) instead; flags as stock (the
// following `seta` reads them, jmp keeps them).
RS7_RESUME_VAR(CC3);
RS7_STUB(Rs7_Stub_CC3) {
    __asm {
        test dword ptr [edx+ecx*4+948h], 1000h
        jmp g_rs7_resume_CC3
    }
}

// CH3 - child eval slots of strings 6/7.
// 0x874D9C in Play_Tracker_AddNoteOrChord 0x874AB0, the store of a child eval slot (EAX) for
// chord string [EBP-0E0Ch] into the parent's subEval (EDX = parent slot * 30Ah, [EBP-0E00h] = parent
// slot, EBX = tracker): strings 0-5 as stock (`add edx,[ebp-0E0Ch]; mov [ebx+edx*4+0C0Ch],eax`),
// strings 6/7 into the byte at parent eval +0C1Eh + (string - 6) (= tracker + slot * 0C28h + 0C26h),
// skipped when the parent got no slot (-1: stock would write in front of the pool).
RS7_RESUME_VAR(CH3);
RS7_STUB(Rs7_Stub_CH3) {
    __asm {
        push ecx
        mov ecx, [ebp-0E0Ch]
        cmp ecx, 6
        jae ch_ext
        add edx, ecx
        mov [ebx+edx*4+0C0Ch], eax
        pop ecx
        jmp g_rs7_resume_CH3
    ch_ext:
        cmp dword ptr [ebp-0E00h], -1
        je ch_done
        lea edx, [ebx+edx*4+0C20h]
        mov [edx+ecx], al
    ch_done:
        pop ecx
        jmp g_rs7_resume_CH3
    }
}

// RL1 / RL2 - release the child evals of strings 6/7 with their parent.
// 0x87467B / 0x87470D in the eval releases 0x874610 / 0x8746D0 (ESI = tracker + slot *
// 0C28h, the parent eval at ESI + 8; EDI = tracker; inside `if isChord`): first the children of strings
// 6/7 (bytes at ESI + 0C26h/0C27h) are released like the stock six (0x8746D0, stdcall slot, ECX =
// tracker) and set to FFh, then the replay `add esi,0C0Ch` and the stock loop over subEval[6].
RS7_RESUME_VAR(RL1);
RS7_RESUME_VAR(RL2);
RS7_VAR(g_rs7_fn_evrel);
#define RS7_NS_RELEASE(name, resume)                                                                                   \
    RS7_STUB(name) {                                                                                                   \
        __asm movzx eax, byte ptr[esi + 0C26h] __asm cmp al, 0FFh __asm je rel_s7 __asm push eax __asm mov ecx,        \
            edi __asm call g_rs7_fn_evrel __asm mov byte ptr[esi + 0C26h], 0FFh __asm rel_s7                           \
            : __asm movzx eax, byte ptr[esi + 0C27h] __asm cmp al,                                                     \
              0FFh __asm je rel_done __asm push eax __asm mov ecx,                                                     \
              edi __asm call g_rs7_fn_evrel __asm mov byte ptr[esi + 0C27h], 0FFh __asm rel_done                       \
            : __asm add esi, 0C0Ch __asm jmp resume                                                                    \
    }
RS7_NS_RELEASE(Rs7_Stub_RL1, g_rs7_resume_RL1)
RS7_NS_RELEASE(Rs7_Stub_RL2, g_rs7_resume_RL2)

// AG1-AG3 - aggregate the child queries over all eight strings.
// entries of the three unrolled child queries 0x875780 (fields 56h/76h, an empty child fails
// the chord), 0x875A10 (54h/74h, empty skipped, EDX = string pointer or 0) and 0x875C90 (55h/75h, same):
// ESI = parent eval, EDI = tracker; AL = result, the rest of EAX and every other register as on entry
// (stock writes only AL). One C++ loop over the eight strings (Rs7Ns_Aggregate).
extern "C" uint8_t __cdecl Rs7Ns_Aggregate(const uint8_t*, const uint8_t*, int, int, const uint8_t*);
#define RS7_NS_AGG(name, field, zero, strptr)                                                                          \
    RS7_STUB(name) {                                                                                                   \
        __asm pushad __asm push strptr __asm push zero __asm push field __asm push edi __asm push esi __asm call       \
            Rs7Ns_Aggregate __asm add esp,                                                                             \
            20 __asm mov[esp + 1Ch], al __asm popad __asm ret                                                          \
    }
RS7_RESUME_VAR(AG1);  // not used: AG1-AG3 replace the whole function
RS7_RESUME_VAR(AG2);
RS7_RESUME_VAR(AG3);
RS7_NS_AGG(Rs7_Stub_AG1, 56h, 1, 0)
RS7_NS_AGG(Rs7_Stub_AG2, 54h, 0, edx)
RS7_NS_AGG(Rs7_Stub_AG3, 55h, 0, edx)

// Loop latches of the per-string technique checks: the stock body runs for strings 0-5, then strings
// 6/7 run the same test on the eval's N-slot tails (midi[6..7] at eval +29Ch, masks at +2A4h; the
// stock body would have rejected them with its string < 6 guard). The flags at the exit are the stock
// ones (the counter compared with 6: ZF=1, CF=0). The pointer register is eval + 2BCh + 4 * 6 there.
//
// UP1 / UP2 / UP3 - "every sounding string unpitched" latches.
// UP1 - 0x87617D in Play_Tracker_Update 0x8760D0 (counter AL, pointer ESI, accumulator CL &= (mask &
// 400000h) != 0 for sounding strings); UP2 - 0x8769CB in 0x876980 (AL, ESI, DL); UP3 - 0x876C6C in
// 0x876C20 (AL, EDI, DL).
#define RS7_NS_ALLUNPITCHED(name, REG, ACC, resume, target)                                                            \
    RS7_STUB(name) {                                                                                                   \
        __asm inc al __asm add REG, 4 __asm cmp al, 6 __asm jb up_loop __asm cmp dword ptr[REG - 2D4h + 29Ch],         \
            0 __asm jle up_s7 __asm test dword ptr[REG - 2D4h + 2A4h], 400000h __asm jnz up_s7 __asm xor ACC,          \
            ACC __asm up_s7                                                                                            \
            : __asm cmp dword ptr[REG - 2D4h + 2A0h], 0 __asm jle up_done __asm test dword ptr[REG - 2D4h + 2A8h],     \
              400000h __asm jnz up_done __asm xor ACC, ACC __asm up_done                                               \
            : __asm cmp al, 6 __asm jmp resume __asm up_loop                                                           \
            : __asm jmp target                                                                                         \
    }
RS7_RESUME_VAR(UP1);
RS7_TARGET_VAR(UP1);
RS7_RESUME_VAR(UP2);
RS7_TARGET_VAR(UP2);
RS7_RESUME_VAR(UP3);
RS7_TARGET_VAR(UP3);
RS7_NS_ALLUNPITCHED(Rs7_Stub_UP1, esi, cl, g_rs7_resume_UP1, g_rs7_target_UP1)
RS7_NS_ALLUNPITCHED(Rs7_Stub_UP2, esi, dl, g_rs7_resume_UP2, g_rs7_target_UP2)
RS7_NS_ALLUNPITCHED(Rs7_Stub_UP3, edi, dl, g_rs7_resume_UP3, g_rs7_target_UP3)

// PD2 / PD3 / PD4 - latches of the "all / no sounding string ..." checks over the eval's per-string
// fields (ESI = eval + 2BCh + 4 * string, string counter CNT, result AL, SCR scratch; the exit flags are
// the stock `cmp CNT,6`): after the stock body ran over strings 0-5, strings 6/7 test the same MASK on
// the tails (midi at eval +29Ch / +2A0h, masks at +2A4h / +2A8h) and fold the answer into AL with SETCC.
//   PD2 0x877D4A, latch of 0x877D20: all sounding strings slide (800h)
//   PD3 0x877DB6, latch of 0x877D60: no sounding string bends / slides / unpitched slides / tremolos
//                                    (1000h|800h|400000h|10h)
//   PD4 0x877DF7, latch of 0x877DD0: all sounding strings tremolo (10h)
#define RS7_NS_LATCH(ID, CNT, SCR, SIZE, MASK, SETCC)                                                                  \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm inc CNT __asm add esi, 4 __asm cmp CNT, 6 __asm jb l_loop __asm cmp dword ptr[esi - 2D4h + 29Ch],        \
            0 __asm jle l_s7 __asm test SIZE ptr[esi - 2D4h + 2A4h], MASK __asm SETCC SCR __asm and al, SCR __asm l_s7 \
            : __asm cmp dword ptr[esi - 2D4h + 2A0h], 0 __asm jle l_done __asm test SIZE ptr[esi - 2D4h + 2A8h],       \
              MASK __asm SETCC SCR __asm and al, SCR __asm l_done                                                      \
            : __asm cmp CNT, 6 __asm jmp g_rs7_resume_##ID __asm l_loop                                                \
            : __asm jmp g_rs7_target_##ID                                                                              \
    }
RS7_RESUME_VAR(PD2);
RS7_TARGET_VAR(PD2);
RS7_RESUME_VAR(PD3);
RS7_TARGET_VAR(PD3);
RS7_RESUME_VAR(PD4);
RS7_TARGET_VAR(PD4);
RS7_NS_LATCH(PD2, cl, dl, dword, 800h, setnz)
RS7_NS_LATCH(PD3, bl, cl, dword, 401810h, setz)
RS7_NS_LATCH(PD4, cl, dl, byte, 10h, setnz)
// PD5 - extreme slide distance over strings 6/7.
// 0x877E56, latch of 0x877E10 (the extreme slide distance slideTo - fret over fretted strings;
// ESI = eval + 0BECh + string, counter BL, result EAX, ECX/EDX scratch): strings 6/7 from the tails
// (fret +2ACh, slideTo +2B0h), the stock min/max rule.
RS7_RESUME_VAR(PD5);
RS7_TARGET_VAR(PD5);
RS7_STUB(Rs7_Stub_PD5) {
    __asm {
        inc bl
        inc esi
        cmp bl, 6
        jb pd5_loop
        push ebx
        xor ebx, ebx
    pd5_next:
        movzx edx, byte ptr [esi+ebx-0BF2h+2ACh]
        test edx, edx
        jle pd5_skip
        movzx ecx, byte ptr [esi+ebx-0BF2h+2B0h]
        sub ecx, edx
        test eax, eax
        jnz pd5_have
        mov eax, ecx
        test eax, eax
    pd5_have:
        jns pd5_pos
        cmp ecx, eax
        jge pd5_pos
        mov eax, ecx
    pd5_pos:
        test eax, eax
        jle pd5_skip
        cmp ecx, eax
        jle pd5_skip
        mov eax, ecx
    pd5_skip:
        inc ebx
        cmp ebx, 2
        jb pd5_next
        pop ebx
        cmp bl, 6
        jmp g_rs7_resume_PD5
    pd5_loop:
        jmp g_rs7_target_PD5
    }
}
