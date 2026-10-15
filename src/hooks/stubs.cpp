// Naked x86 stubs for the code sites of sites.json (the ref/callref sites X_* only bind variables).
// Exported (with their resume/target variables) so tools/tests/test_emu.py can run them in Unicorn.
//
// Diagnostics (D1-D5, docs/ARCHITECTURE.md): every call into C is wrapped in
// pushfd/pushad ... popad/popfd, and the FPU stack is the same at the call as at the site (the C
// loggers are integer-only). The hooked functions use no XMM registers (checked with capstone
// over 0x79E470, 0x7A90B0, 0x7A4B50, 0x40CAA0, 0x7F2F10), so the CRT's volatile XMM use is safe.
//
// g_rs7_diag ([debug] diagnostics, set before patching): 0 = R2/R1 skip the D1/D4 calls; the
// D2/D3/D5 stubs are then never reached (Rs7_Startup does not write their sites).
#include "core/assetload.h"
#include "core/chord7.h"
#include "core/diag.h"
#include "core/extblock.h"
#include "core/fretgrid.h"
#include "core/frets.h"
#include "core/headstock.h"
#include "core/mode.h"
#include "core/neck.h"
#include "core/publish_grid.h"
#include "core/songend.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "core/tuner.h"
#include "core/tuningname.h"
#include "core/vguit.h"
#include "hooks/registry.h"
#include <cstdint>

#define RS7_VAR(name) extern "C" __declspec(dllexport) uintptr_t name = 0
#define RS7_STUB(name) extern "C" __declspec(dllexport) __declspec(naked) void name()

RS7_VAR(g_rs7_diag);
RS7_VAR(g_rs7_target_S1);
RS7_VAR(g_rs7_resume_S2);
RS7_VAR(g_rs7_target_S2);
RS7_VAR(g_rs7_resume_I1);
RS7_VAR(g_rs7_target_I1);
RS7_VAR(g_rs7_target_S4);
RS7_VAR(g_rs7_resume_R2);
RS7_VAR(g_rs7_resume_R3a);
RS7_VAR(g_rs7_resume_R3b);
RS7_VAR(g_rs7_resume_R3c);
RS7_VAR(g_rs7_resume_R4);
RS7_VAR(g_rs7_resume_R6);
RS7_VAR(g_rs7_resume_N1);
RS7_VAR(g_rs7_resume_N5);
RS7_VAR(g_rs7_resume_N12);
RS7_VAR(g_rs7_target_N12);
RS7_VAR(g_rs7_resume_N13);
RS7_VAR(g_rs7_target_N13);
RS7_VAR(g_rs7_resume_N18);
RS7_VAR(g_rs7_target_N18);
RS7_VAR(g_rs7_resume_N19);
RS7_VAR(g_rs7_target_N19);
RS7_VAR(g_rs7_resume_N20);
RS7_VAR(g_rs7_resume_PSL1);
RS7_VAR(g_rs7_target_PSL2);
RS7_VAR(g_rs7_resume_PSA2);
RS7_VAR(g_rs7_resume_PSD1);
RS7_VAR(g_rs7_resume_PSD2);
RS7_VAR(g_rs7_resume_PSU1);
RS7_VAR(g_rs7_target_PSU1);
RS7_VAR(g_rs7_resume_PSU2);
RS7_VAR(g_rs7_target_PSU2);
RS7_VAR(g_rs7_resume_PSU3);
RS7_VAR(g_rs7_resume_PSM1);
RS7_VAR(g_rs7_target_PSM1);
RS7_VAR(g_rs7_resume_PSM2);
RS7_VAR(g_rs7_target_PSM2);
RS7_VAR(g_rs7_resume_PSR1);
RS7_VAR(g_rs7_resume_PSN1);
RS7_VAR(g_rs7_target_PSN2);
RS7_VAR(g_rs7_target_N20);
RS7_VAR(g_rs7_resume_N21);
RS7_VAR(g_rs7_resume_N22);
RS7_VAR(g_rs7_target_N22);
RS7_VAR(g_rs7_resume_N23);
RS7_VAR(g_rs7_target_N23);
RS7_VAR(g_rs7_resume_F1);
RS7_VAR(g_rs7_resume_LT1);
RS7_VAR(g_rs7_resume_TF1);
RS7_VAR(g_rs7_target_IN1);
RS7_VAR(g_rs7_resume_AL1);
RS7_VAR(g_rs7_colormgr);
RS7_VAR(g_rs7_texlist_cs);
RS7_VAR(g_rs7_texlist_head);
RS7_VAR(g_rs7_resume_D2);
RS7_VAR(g_rs7_resume_D3);
RS7_VAR(g_rs7_resume_D5);
RS7_VAR(g_rs7_resume_V2);
RS7_VAR(g_rs7_resume_BD1);
RS7_VAR(g_rs7_target_BD1);
RS7_VAR(g_rs7_resume_BD2);
RS7_VAR(g_rs7_target_BD2);
RS7_VAR(g_rs7_resume_BD3);
RS7_VAR(g_rs7_target_BD3);
RS7_VAR(g_rs7_resume_BD4);
RS7_VAR(g_rs7_target_BD4);
extern "C" __declspec(dllexport) float g_rs7_v6[2] = {0.0f, 0.0f};
extern "C" __declspec(dllexport) float g_rs7_fx_rgb[6] = {};

extern "C" __declspec(dllexport) float g_rs7_scratch_R3a[3] = {};
extern "C" __declspec(dllexport) float g_rs7_scratch_R3b[3] = {};
extern "C" __declspec(dllexport) float g_rs7_scratch_R3c[3] = {};
extern "C" __declspec(dllexport) float g_rs7_scratch_R4[3] = {};

// S1 - target of the guitar JA (string > 5) in 0x873F80. EAX = -1, ECX = tuning, ESI = string.
// g_rs7_target_S1 = 0x874020 (add eax,[ebp+8]; pop ebp; ret).
RS7_STUB(Rs7_Stub_S1) {
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
RS7_STUB(Rs7_Stub_S2) {
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

// I1 - replaces `sub eax,2; jz bass` in Play_MidiToFret 0x874050 (inverse pitch feedback, called
// from Play_AnalyzeNoteMistake 0x875513: ECX = played MIDI, EDX = string, ESI = detector tuning
// short* (detector+1340h, the side-store key), [ebp+8] = instrument). Stock reads
// guitar-base[6] = bass-base[0] = 28 and tuning[6] past the six entries. String 6 of an active
// 7-string song: fret = MIDI - (35 + t6) (the inverse of S1/S2). Everything else replays the stock
// instructions. g_rs7_resume_I1 = 0x87405B (test esi,esi), g_rs7_target_I1 = 0x874079 (bass).
RS7_STUB(Rs7_Stub_I1) {
    __asm {
        sub eax, 2
        jz i1_bass
        cmp edx, 6
        jne i1_stock
        test esi, esi
        jz i1_stock
        push ecx
        push edx
        push esi
        call Rs7_Base6
        add esp, 4
        pop edx
        pop ecx
        cmp eax, 80000000h
        je i1_stock
        sub ecx, eax
        mov eax, ecx
        pop ebp
        ret
    i1_stock:
        jmp g_rs7_resume_I1
    i1_bass:
        jmp g_rs7_target_I1
    }
}

// S3 - replaces the last store of Play_Detector_SetTuning (guitar); EAX = detector, ESI = the
// std::vector<int16> tuning it just copied (0x4E2590 only reads ESI, at 0x4E2593/0x4E2595).
RS7_STUB(Rs7_Stub_S3) {
    __asm {
        mov word ptr [eax+1356h], cx
        pushad
        push esi
        push eax
        call Rs7_OnDetectorTuned
        add esp, 8
        popad
        ret
    }
}

// S4 - the song-setup call to Play_Detector_SetTuning: EAX = detector, ESI = SNG tuning
// vector, EBX = session. g_rs7_target_S4 = 0x4E2590.
RS7_STUB(Rs7_Stub_S4) {
    __asm {
        pushad
        push esi
        push eax
        push ebx
        call Rs7_OnSongTuning
        add esp, 12
        popad
        jmp g_rs7_target_S4
    }
}

// R2 - note head lane z: `add edx,[ebp+1Ch]; fld [ebx+edx+44h]`, EBX = string * 4Ch.
// String 6: 2 * cell0.z - cell1.z. The `add` is the last flag writer so
// the flags equal stock.
// D1 (strings 0 and 6): the lane z goes through a stack slot (no global, so thread-safe; FPU
// depth never above stock's +1) and |z - floor [ebp-30h]| (as the game computes it at 0x79E749)
// into a second one; both are logged with string [ebp+14h], fret [ebp+18h], open [ebp+10h] and
// the row (z0 +44h, z1 +90h), then the lane z is pushed like stock and the slots are released
// with `lea` (flags of the `add` kept). Stock rounds z to float32 next anyway (fstp [ebp-24h]).
// Diagnostics off (g_rs7_diag 0): string 0 runs the stock replay, string 6 skips D1 (the check
// sits inside pushfd/popfd, so the flags of the `add` survive).
RS7_STUB(Rs7_Stub_R2) {
    __asm {
        cmp ebx, 1C8h
        je r2_six
        test ebx, ebx
        jne r2_stock
        cmp dword ptr [g_rs7_diag], 0
        jne r2_zero
    r2_stock:
        add edx, [ebp+1Ch]
        fld dword ptr [ebx+edx+44h]
        jmp g_rs7_resume_R2
    r2_six:
        add edx, [ebp+1Ch]
        fld dword ptr [edx+44h]
        fadd st(0), st(0)
        fsub dword ptr [edx+90h]
        jmp r2_log
    r2_zero:
        add edx, [ebp+1Ch]
        fld dword ptr [edx+44h]
    r2_log:
        pushfd
        cmp dword ptr [g_rs7_diag], 0
        je r2_nolog
        popfd
        lea esp, [esp-8]  // [esp] lane z, [esp+4] |z - floor| (lea: flags kept)
        fstp dword ptr [esp]
        fld dword ptr [esp]
        fsub dword ptr [ebp-30h]
        fabs
        fstp dword ptr [esp+4]
        pushfd
        pushad
        mov esi, esp  // [esi+36] lane z, [esi+40] |z - floor|
        push dword ptr [esi+40]
        push dword ptr [esi+36]
        push dword ptr [ebp-30h]
        push edx
        movzx eax, byte ptr [ebp+10h]
        push eax
        movzx eax, byte ptr [ebp+18h]
        push eax
        movzx eax, byte ptr [ebp+14h]
        push eax
        call Rs7_DiagNoteHead
        mov esp, esi
        popad
        popfd
        fld dword ptr [esp]
        lea esp, [esp+8]
        jmp g_rs7_resume_R2
    r2_nolog:
        popfd
        jmp g_rs7_resume_R2
    }
}

// R3a/R3b/R3c - NoteFx position: `lea eax,[ecx+eax+3Ch]; fld [eax]`, ECX = string * 4Ch.
// String 6: EAX -> scratch vec3 = 2 * cell0 - cell1. lea/fld leave the flags alone, so the compares are wrapped in
// pushfd/popfd.
RS7_STUB(Rs7_Stub_R3a) {
    __asm {
        pushfd
        cmp ecx, 1C8h
        je r3a_six
        popfd
        lea eax, [ecx+eax+3Ch]
        fld dword ptr [eax]
        jmp g_rs7_resume_R3a
    r3a_six:
        popfd
        fld dword ptr [eax+3Ch]
        fadd st(0), st(0)
        fsub dword ptr [eax+88h]
        fstp dword ptr [g_rs7_scratch_R3a]
        fld dword ptr [eax+40h]
        fadd st(0), st(0)
        fsub dword ptr [eax+8Ch]
        fstp dword ptr [g_rs7_scratch_R3a+4]
        fld dword ptr [eax+44h]
        fadd st(0), st(0)
        fsub dword ptr [eax+90h]
        fstp dword ptr [g_rs7_scratch_R3a+8]
        lea eax, g_rs7_scratch_R3a
        fld dword ptr [eax]
        jmp g_rs7_resume_R3a
    }
}

RS7_STUB(Rs7_Stub_R3b) {
    __asm {
        pushfd
        cmp ecx, 1C8h
        je r3b_six
        popfd
        lea eax, [ecx+eax+3Ch]
        fld dword ptr [eax]
        jmp g_rs7_resume_R3b
    r3b_six:
        popfd
        fld dword ptr [eax+3Ch]
        fadd st(0), st(0)
        fsub dword ptr [eax+88h]
        fstp dword ptr [g_rs7_scratch_R3b]
        fld dword ptr [eax+40h]
        fadd st(0), st(0)
        fsub dword ptr [eax+8Ch]
        fstp dword ptr [g_rs7_scratch_R3b+4]
        fld dword ptr [eax+44h]
        fadd st(0), st(0)
        fsub dword ptr [eax+90h]
        fstp dword ptr [g_rs7_scratch_R3b+8]
        lea eax, g_rs7_scratch_R3b
        fld dword ptr [eax]
        jmp g_rs7_resume_R3b
    }
}

RS7_STUB(Rs7_Stub_R3c) {
    __asm {
        pushfd
        cmp ecx, 1C8h
        je r3c_six
        popfd
        lea eax, [ecx+eax+3Ch]
        fld dword ptr [eax]
        jmp g_rs7_resume_R3c
    r3c_six:
        popfd
        fld dword ptr [eax+3Ch]
        fadd st(0), st(0)
        fsub dword ptr [eax+88h]
        fstp dword ptr [g_rs7_scratch_R3c]
        fld dword ptr [eax+40h]
        fadd st(0), st(0)
        fsub dword ptr [eax+8Ch]
        fstp dword ptr [g_rs7_scratch_R3c+4]
        fld dword ptr [eax+44h]
        fadd st(0), st(0)
        fsub dword ptr [eax+90h]
        fstp dword ptr [g_rs7_scratch_R3c+8]
        lea eax, g_rs7_scratch_R3c
        fld dword ptr [eax]
        jmp g_rs7_resume_R3c
    }
}

// R4 - feedback position: `lea eax,[ecx+eax+3Ch]; pop esi; fld [eax]`.
RS7_STUB(Rs7_Stub_R4) {
    __asm {
        pushfd
        cmp ecx, 1C8h
        je r4_six
        popfd
        lea eax, [ecx+eax+3Ch]
        pop esi
        fld dword ptr [eax]
        jmp g_rs7_resume_R4
    r4_six:
        popfd
        fld dword ptr [eax+3Ch]
        fadd st(0), st(0)
        fsub dword ptr [eax+88h]
        fstp dword ptr [g_rs7_scratch_R4]
        fld dword ptr [eax+40h]
        fadd st(0), st(0)
        fsub dword ptr [eax+8Ch]
        fstp dword ptr [g_rs7_scratch_R4+4]
        fld dword ptr [eax+44h]
        fadd st(0), st(0)
        fsub dword ptr [eax+90h]
        fstp dword ptr [g_rs7_scratch_R4+8]
        lea eax, g_rs7_scratch_R4
        pop esi
        fld dword ptr [eax]
        jmp g_rs7_resume_R4
    }
}

// R6 - lane offset y: `add eax,edi; fld [ecx+eax+40h]`, ECX = string * 4Ch. `add` stays last.
RS7_STUB(Rs7_Stub_R6) {
    __asm {
        cmp ecx, 1C8h
        je r6_six
        add eax, edi
        fld dword ptr [ecx+eax+40h]
        jmp g_rs7_resume_R6
    r6_six:
        add eax, edi
        fld dword ptr [eax+40h]
        fadd st(0), st(0)
        fsub dword ptr [eax+8Ch]
        jmp g_rs7_resume_R6
    }
}

// ---- 7th string line (core/string7.h, the native strings in core/gsnative.h) ----
// All C calls sit inside pushad/popad with the replay last; at every site the x87 stack is empty
// (function entries, or right after a call). No XMM register is live in any host function (checked with capstone).

// N1 (0x5965CA) and N21 (0x5965E9), the pool request counts of Rnd_Guitar_LoadDependencies, use C++
// mid-hook callbacks (mid_cold.cpp).

// N5 - 0x592AD4 in Rnd_Guitar_Update (EBX = Rnd_Guitar; also the target of the `je` when
// GuitarStrings is null): Rs7_S7Update, then the replay `mov eax,[ebx+18h]; test eax,eax` (its
// flags feed the `je` at 0x592AD9).
RS7_STUB(Rs7_Stub_N5) {
    __asm {
        pushad
        push ebx
        call Rs7_S7Update
        add esp, 4
        popad
        mov eax, [ebx+18h]
        test eax, eax
        jmp g_rs7_resume_N5
    }
}

// ---- 7th string highlight: the stock section dimming (docs/ARCHITECTURE.md,
// "Highlight of the 7th string") ----

// N12 - 0x55CC71 in SNG PostProcessNotes, single-note branch (CL = the note's string): stock
// `cmp cl,6; jae skip` keeps string 6 out of Section.stringMask[level]. With [neck] string7_line on,
// string 6 goes on to the stock write (its own level < 32 guard follows at 0x55CC76, then
// `or [mask], 1 << 6`). Stock songs have no string-6 notes, so their masks are unchanged. The
// flags are dead at both continuations (the next instruction is a cmp / mov).
RS7_STUB(Rs7_Stub_N12) {
    __asm {
        cmp cl, 6
        jb n12_go
        ja n12_skip
        cmp dword ptr [g_rs7_s7_on], 0
        je n12_skip
    n12_go:
        jmp g_rs7_resume_N12
    n12_skip:
        jmp g_rs7_target_N12
    }
}

// N13 - 0x55CE3A, the bound of the back-fill loop over strings 0-5 in SNG PostProcessNotes (CL =
// string, EDI = level, ESI = the SNG song; per level the back-fill lights a string one section
// early). Strings 0-5 loop on as stock. After string 5, Rs7_S7MaskBackfill does the same for bit 6
// of this level (integer-only; a no-op without string-6 notes, i.e. in every stock song), then
// the stock continuation 0x55CE43. The stock loop body cannot be reused for string 6: its
// `cl >= 6` paths skip the level guard (null write for level >= 32) and the OR.
RS7_STUB(Rs7_Stub_N13) {
    __asm {
        cmp cl, 6
        jb n13_loop
        pushad
        push edi
        push esi
        call Rs7_S7MaskBackfill
        add esp, 8
        popad
        jmp g_rs7_resume_N13
    n13_loop:
        jmp g_rs7_target_N13
    }
}

// ---- 7th string bend (issue #16, core/string7.h Rs7_S7BendTarget) ----

// N18 - 0x7E3FFA in the note-in-progress update 0x7E3700, single-note bend: `cmp al,[esi+0Bh];
// jae 0x7E4095` (AL = note string, ESI = guitar info, ECX = GuitarStrings, DL = note fret). Stock
// continues with `cmp dl,0FFh; je; movzx edx,al; mov ecx,[ecx+edx*4+4]` (the string object), then
// at 0x7E4013 `cmp [ecx+4],0; jz` and the call of Rnd_GuitarString_SetBend 0x598910 (ECX = string,
// (fret, amount [ebp-64h], speed 2.0)). Strings 0-5 replay `cmp; jae` (resume = 0x7E4003, target
// as stock). String 6 with DL != 0xFF on a native GuitarStrings built with six (Rs7_S7BendTarget): ECX = its
// string 6, EDX = 6, join at 0x7E4013 (resume + 10h), so the stock bend call plays its `bendup_NN` sequence.
// x87 is empty at the site; flags are dead after both continuations.
RS7_STUB(Rs7_Stub_N18) {
    __asm {
        cmp al, [esi+0Bh]
        jb n18_stock
        cmp al, 6
        jne n18_target
        cmp dl, 0FFh
        je n18_target
        pushad
        push dword ptr [ebp-64h]  // amount (float bits), log only
        movzx edx, dl
        push edx  // fret, log only
        push 0  // kind 0: bend
        push ecx  // GuitarStrings
        call Rs7_S7BendTarget
        add esp, 16
        test eax, eax
        jz n18_none
        mov [esp+28], eax  // the EAX slot of pushad
        popad
        mov ecx, eax
        mov edx, 6
        mov eax, dword ptr [g_rs7_resume_N18]
        add eax, 10h
        jmp eax
    n18_none:
        popad  // no string 6 object: every register as stock
        jmp g_rs7_target_N18
    n18_stock:
        jmp g_rs7_resume_N18
    n18_target:
        jmp g_rs7_target_N18
    }
}

// N19 - 0x7E4059, the same function's bend release (no bend at this time): `cmp al,[edx+0Bh];
// jae 0x7E4095` (AL = note string, EDX = guitar info, ECX = GuitarStrings); stock continues with
// `movzx eax,al; mov eax,[ecx+eax*4+4]`, then at 0x7E4065 `cmp byte [eax+0BDh],0FFh; je` and sets
// the string's bend target to 0 (+0BCh = 1, +0C0h = 0.0, +0C4h = 5.0). Strings 0-5 replay `cmp;
// jae`. String 6 with a native string-6 object: EAX = it, join at 0x7E4065 (resume + 7). Flags are dead.
RS7_STUB(Rs7_Stub_N19) {
    __asm {
        cmp al, [edx+0Bh]
        jb n19_stock
        cmp al, 6
        jne n19_target
        pushad
        push 0
        push 0
        push 1  // kind 1: release
        push ecx  // GuitarStrings
        call Rs7_S7BendTarget
        add esp, 16
        test eax, eax
        jz n19_none
        mov [esp+28], eax
        popad
        push dword ptr [g_rs7_resume_N19]
        add dword ptr [esp], 7
        ret
    n19_none:
        popad
        jmp g_rs7_target_N19
    n19_stock:
        jmp g_rs7_resume_N19
    n19_target:
        jmp g_rs7_target_N19
    }
}

// N20 - 0x7E43DB in the note-end update 0x7E4230, the bend release of a bend note (flag 1000h) when
// it ends: the same `cmp al,[edx+0Bh]; jae 0x7E4443` / `movzx eax,al; mov eax,[ecx+eax*4+4]` /
// 0x7E43E7 `cmp byte [eax+0BDh],0FFh` shape as N19 (ST0 = the target 0.0 is live: one x87 slot
// during the call). String 6 with a native string-6 object: EAX = it, join at resume + 7.
RS7_STUB(Rs7_Stub_N20) {
    __asm {
        cmp al, [edx+0Bh]
        jb n20_stock
        cmp al, 6
        jne n20_target
        pushad
        push 0
        push 0
        push 1  // kind 1: release
        push ecx  // GuitarStrings
        call Rs7_S7BendTarget
        add esp, 16
        test eax, eax
        jz n20_none
        mov [esp+28], eax
        popad
        push dword ptr [g_rs7_resume_N20]
        add dword ptr [esp], 7
        ret
    n20_none:
        popad
        jmp g_rs7_target_N20
    n20_stock:
        jmp g_rs7_resume_N20
    n20_target:
        jmp g_rs7_target_N20
    }
}

// N22 - 0x7E3ED5 in Rnd_Note_UpdateInProgress 0x7E3700, the chart bend on the ghost strings:
// `cmp al,[esi+0Bh]; jae 0x7E3F14` (AL = string, ESI = guitar info, ECX = GuitarStrings, DL =
// fret); stock continues `cmp dl,0FFh; je; movzx edx,al; mov ecx,[ecx+edx*4+1Ch]` (stringObj2[s])
// and at 0x7E3EE6 `cmp [ecx+4],0; jz`, then Rnd_GuitarString_SetBend(fret, amount*2/3, 2.0) =
// "ghost_up_NN" on the ghost. String 6 with DL != 0xFF and a ready S7G: ECX = S7G, EDX = 6, join
// at 0x7E3EE6 (resume + 0Ch). Flags are dead after both continuations.
RS7_STUB(Rs7_Stub_N22) {
    __asm {
        cmp al, [esi+0Bh]
        jb n22_stock
        cmp al, 6
        jne n22_target
        cmp dl, 0FFh
        je n22_target
        pushad
        push dword ptr [ebp-60h]  // the chart bend amount before *2/3 (float bits), log only
        movzx edx, dl
        push edx
        push 2  // kind 2: ghost bend
        push ecx
        call Rs7_S7BendTarget
        add esp, 16
        test eax, eax
        jz n22_none
        mov [esp+28], eax
        popad
        mov ecx, eax
        mov edx, 6
        mov eax, dword ptr [g_rs7_resume_N22]
        add eax, 0Ch
        jmp eax
    n22_none:
        popad
        jmp g_rs7_target_N22
    n22_stock:
        jmp g_rs7_resume_N22
    n22_target:
        jmp g_rs7_target_N22
    }
}

// N23 - 0x7E4347 in Rnd_Note_UpdateEnd 0x7E4230, the ghost bend release of an ending note: `cmp
// cl,[esi+0Bh]; jae 0x7E4373` (CL = string, EDX = GuitarStrings); stock continues `movzx ecx,cl;
// mov ecx,[edx+ecx*4+1Ch]` and at 0x7E4353 `cmp byte [ecx+0BDh],0FFh` (two x87 slots live: 1.0
// and 0.0, one call of ours in between). String 6 with a ready S7G: ECX = S7G, join at resume + 7.
RS7_STUB(Rs7_Stub_N23) {
    __asm {
        cmp cl, [esi+0Bh]
        jb n23_stock
        cmp cl, 6
        jne n23_target
        pushad
        push 0
        push 0
        push 3  // kind 3: ghost release
        push edx
        call Rs7_S7BendTarget
        add esp, 16
        test eax, eax
        jz n23_none
        mov [esp+24], eax  // the ECX slot of pushad
        popad
        push dword ptr [g_rs7_resume_N23]
        add dword ptr [esp], 7
        ret
    n23_none:
        popad
        jmp g_rs7_target_N23
    n23_stock:
        jmp g_rs7_resume_N23
    n23_target:
        jmp g_rs7_target_N23
    }
}

// F1 - entry of Rnd_FretBars_ApplyTransforms 0x58FB20 (EAX = FretGrid, ECX = guitar; no stack
// arguments, plain `ret`, x87 empty at both ends). The stock body runs as a subroutine (replayed
// `push ebp; mov ebp,esp; sub esp,34h`, then site + 6); after its `ret` Rs7_FretsAfterApply(grid)
// re-places the bars of a guitar that shows the 7th string. The registers the stock body returns
// with are kept (pushad/popad); flags are dead after a call.
RS7_STUB(Rs7_Stub_F1) {
    __asm {
        push eax
        call f1_stock
        pushad
        push dword ptr [esp+32]
        call Rs7_FretsAfterApply
        add esp, 4
        popad
        add esp, 4
        ret
    f1_stock:
        push ebp
        mov ebp, esp
        sub esp, 34h
        jmp g_rs7_resume_F1
    }
}

// IN1 - the call of Rnd_Inlays_ApplyTransforms 0x59BA80 at 0x593074 (EAX = guitar, [esp+4] = the inlay set, ret 4):
// the stock call, then Rs7_InlaysAfter(guitar, set) re-places the inlay entity for a guitar built with 7/8 strings.
RS7_STUB(Rs7_Stub_IN1) {
    __asm {
        push eax
        push dword ptr [esp+8]
        call dword ptr [g_rs7_target_IN1]
        pop eax
        pushad
        push dword ptr [esp+36]
        push eax
        call Rs7_InlaysAfter
        add esp, 8
        popad
        ret 4
    }
}

// TF1 - 0x5B26B5 in the tuner camera pose 0x5B2680 (EAX = the guitar; not inverted): the stock reference string
// `mov edx,[eax+24h]; mov bl,[edx+0Bh]; dec bl` = count-1, the string at the edge opposite string 0. On a guitar
// built with 7/8 strings, strings 6/7 lie beyond string 0 (the extrapolated lanes), so that edge is still string 5:
// BL = min(count, 6) - 1. Bass (4) and six-string guitars: unchanged. Back to 0x5B26BD.
RS7_STUB(Rs7_Stub_TF1) {
    __asm {
        mov edx, [eax+24h]
        mov bl, [edx+0Bh]
        cmp bl, 6
        jbe tf1_count
        mov bl, 6
    tf1_count:
        dec bl
        jmp g_rs7_resume_TF1
    }
}

// LT1 - 0x5B1B53 in UI_PlayerTuner_PlacePegLetters 0x5B1A10 (EDI = InstrumentInfo): `xor al,al;
// cmp [edi+0Ah],al` (+0Ah = the in-line flag, CONFIRMED 0x58ABEC) decides which tuner letters are turned
// round (3+3: the pegs from count/2 on). Rs7Asset_LetterInline answers 1 for a guitar that shows the 7-string
// in-line headstock on a 3+3 frame, so its letters follow the in-line rule; else the stock byte.
RS7_VAR(g_rs7_lt1_inline);
RS7_STUB(Rs7_Stub_LT1) {
    __asm {
        pushad
        push edi
        call Rs7Asset_LetterInline
        add esp, 4
        mov g_rs7_lt1_inline, eax
        popad
        xor al, al
        cmp byte ptr [g_rs7_lt1_inline], al
        jmp g_rs7_resume_LT1
    }
}

// ---- native N-slot PegSet (core/headstock.h, issue #18 stage 3) ----

// The 8-slot capacity of PSA2 / PSD1 / PSD2 / PSU1-PSU3 / PSM1 / PSM2 / PSL1 applies only to a native
// PegSet: one registered in g_rs7_pegsets by Rs7_PegSetCtor (PSC1). A PegSet constructed before the
// patch (the boot's first guitar) has the stock 0x15C layout: its pegColor[6] at +0FCh is where slots
// 6/7 would be, so an 8-slot unload read a colour float as an entity (measured 2026-10-13: AV at
// 0x59C3E7, ECX 3F800000, title -> main menu). Those keep every stock count. The decision never reads
// the PegSet (review #1: the old magic check at +1D0h read 74h bytes past a stock 0x15C allocation).

// Rs7_PegOwnedZF - stdcall(PegSet), ret 4: ZF = 1 iff the PegSet is in g_rs7_pegsets (0 never is).
// Every register is preserved (pop / ret leave the flags); only EFLAGS changes. The callers' flags are
// dead at every PS* site (each old check began with a cmp). 64 entries = RS7_PEG_OWNERS (static_assert
// in core/headstock.cpp).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_PegOwnedZF() {
    __asm {
        push ecx
        push eax
        mov eax, dword ptr [esp+0Ch]
        test eax, eax
        jz pown_no
        mov ecx, 64
    pown_scan:
        cmp eax, dword ptr [g_rs7_pegsets + ecx*4 - 4]
        je pown_yes
        dec ecx
        jnz pown_scan
    pown_no:
        or ecx, 1
    pown_yes:
        pop eax
        pop ecx
        ret 4
    }
}

// PSA2 (0x58DA05, the PegSet's sized free) uses a C++ mid-hook callback (mid_cold.cpp).

// PSD1 (0x59C30D) and PSD2 (0x59C314), the PegSet destructor's release counter and anchor, use C++ mid-hook
// callbacks (mid_cold.cpp).

// PSU1 - 0x59C3C2 in Rnd_PegSet_Unload 0x59C370 (EDI = PegSet, CX = 0): the give-back loop entry
// `cmp cx,[edi+4]; jae 0x59C41A` - bound 8 for a native PegSet (every slot with an entity, also
// above the active count). Flags dead on both exits.
RS7_STUB(Rs7_Stub_PSU1) {
    __asm {
        push edi
        call Rs7_PegOwnedZF
        jne psu1_stock
        cmp cx, 8
        jae psu1_done
        jmp g_rs7_resume_PSU1
    psu1_stock:
        cmp cx, word ptr [edi+4]
        jae psu1_done
        jmp g_rs7_resume_PSU1
    psu1_done:
        jmp g_rs7_target_PSU1
    }
}

// PSU2 - 0x59C414 in 0x59C370 (EDI = PegSet): the loop latch `cmp bx,[edi+4]; jb 0x59C3D0` - bound 8
// for a native PegSet.
RS7_STUB(Rs7_Stub_PSU2) {
    __asm {
        push edi
        call Rs7_PegOwnedZF
        jne psu2_stock
        cmp bx, 8
        jb psu2_loop
        jmp g_rs7_resume_PSU2
    psu2_stock:
        cmp bx, word ptr [edi+4]
        jb psu2_loop
        jmp g_rs7_resume_PSU2
    psu2_loop:
        jmp g_rs7_target_PSU2
    }
}

// PSU3 (0x59C41D, the unload's taken-flag clear count) uses a C++ mid-hook callback (mid_cold.cpp).

// PSM1 - 0x541B99 in Rnd_StringRenderer_Setup 0x541880 (EAX = PegSet, CX = pass): the peg mesh
// collection's slot bound `cmp cx,[eax+4]; jae 0x541D2B` - 8 for a native PegSet. Flags dead.
RS7_STUB(Rs7_Stub_PSM1) {
    __asm {
        push eax
        call Rs7_PegOwnedZF
        jne psm1_stock
        cmp cx, 8
        jae psm1_skip
        jmp g_rs7_resume_PSM1
    psm1_stock:
        cmp cx, word ptr [eax+4]
        jae psm1_skip
        jmp g_rs7_resume_PSM1
    psm1_skip:
        jmp g_rs7_target_PSM1
    }
}

// PSM2 (0x541D34, the peg mesh collection pass latch) uses a C++ mid-hook callback (mid_cold.cpp). PSU1,
// PSU2 and PSM1 stay here: their displaced `cmp r16,[mem]` has an operand-size prefix the relocator rejects.

// PSC1 (0x59C2F4, the PegSet constructor tail) and PSL1 (0x59C4D0, the load count) use C++ mid-hook
// callbacks (mid_cold.cpp).

// PSL2 - the call of 0x59CBC0 at 0x59C60C, the end of a completed Rnd_PegSet_Load (EBX = PegSet,
// [esp+4] = the pushed PegSet, callee ret 4): the stock call, the stock `mov byte [ebx+8],1` (the
// game repeats it after the return), Rs7_PegSetLoaded (loaded count kept, active count 6, peg 7
// culled), ret 4. Not a jmp over 0x59C611: 0x59C587 `jne 0x59C615` ([ebp+8] != 2) enters the
// second instruction there, and under `E9 rel32 90 90` that is the rel32's top byte - 0x5D for any
// stub in this DLL (fixed base 0x5E000000) = `pop ebp`: EBP = the saved EDI, `mov esp,ebp; pop ebp;
// ret 8` returned through the caller's EDI object (often to 0) - the issue #18 boot crash.
RS7_STUB(Rs7_Stub_PSL2) {
    __asm {
        push dword ptr [esp+4]
        call g_rs7_target_PSL2
        mov byte ptr [ebx+8], 1
        pushad
        push ebx
        call Rs7_PegSetLoaded
        add esp, 4
        popad
        ret 4
    }
}

// PSR1 (0x5B0A21, the colour reset loop bound) and PSN1 (0x5B089D, the colour name lookup) use C++ mid-hook
// callbacks (mid_cold.cpp).

// PSN2 - the call of the named-colour dispatch 0x59BF80 at 0x5B08F6 (EAX = name, ECX = player,
// [esp+4] = slot or NULL, ret 4; EBX = peg index, ESI = session, both callee-saved by 0x59BF80). A
// NULL slot (index not active / not taken; stock never passes one for its six) is skipped instead of
// dereferenced. Index < 6: the stock call. Index >= 6: the stock call, then Rs7_PegNamed.
RS7_STUB(Rs7_Stub_PSN2) {
    __asm {
        cmp dword ptr [esp+4], 0
        je psn2_skip
        cmp ebx, 6
        jae psn2_ext
        jmp g_rs7_target_PSN2
    psn2_ext:
        push dword ptr [esp+4]
        call g_rs7_target_PSN2
        pushad
        push ebx
        push esi
        call Rs7_PegNamed
        add esp, 8
        popad
    psn2_skip:
        ret 4
    }
}

// V2 - 0x7A69BE in the slide offset 0x7A6720: the end cell read `movzx eax,al; imul eax,eax,1CCh; add eax,ecx;
// lea eax,[edx+eax+3Ch]` (EDX = string*4Ch, AL = fret-1, ECX = FretGrid; EAX = &cell + 3Ch, then three fld
// [eax+0/4/8]). The site is only the stride carrier: its 1CCh immediate is excluded from the in-place FretGrid
// edits (tools/gen_fretgrid.py EXCLUDED), so the stub multiplies by g_rs7_fg_row (1CCh stock, 264h with
// native_fretgrid). Strings 0-7 are real cells now ([neck] native_publish); there is no string-6 extrapolation.
RS7_STUB(Rs7_Stub_V2) {
    __asm {
        movzx eax, al
        imul eax, dword ptr [g_rs7_fg_row]
        add eax, ecx
        lea eax, [edx+eax+3Ch]
        jmp g_rs7_resume_V2
    }
}

// BD1-BD4 - the four `cmp <string>,6; jae` guards in front of a perStringMode read (info +20h +
// string*4: 1 for strings 0-2, 0 for 3-5; it picks the bend direction). Stock treats string >= 6
// as mode 2 (no offset / the other branch), so a string-6 bend went the wrong way (owner, issue
// #14). String 6 now reads perStringMode[0], string 0's direction (it sits next to string 0); other
// strings replay `cmp; jae` exactly (strings 0-5 -> the stock body at the resume address, > 6 ->
// the stock target). Each string-6 path does what the stock body does with index 0 and joins the
// stock code after it.
RS7_STUB(Rs7_Stub_BD1) {  // 0x79FE9B: body `movzx edx,cl; cmp dword [eax+edx*4+20h],0; jne 0x79FEB7`
    __asm {
        cmp cl, 6
        jb bd1_stock
        jne bd1_target
        movzx edx, cl
        cmp dword ptr [eax+20h], 0
        jne bd1_target
        push g_rs7_resume_BD1
        add dword ptr [esp], 0Ah           // 0x79FEAA: the mode-0 continuation (after the jne at +8)
        ret
    bd1_stock:
        jmp g_rs7_resume_BD1
    bd1_target:
        jmp g_rs7_target_BD1
    }
}

RS7_STUB(Rs7_Stub_BD2) {  // 0x7A663B: body `movzx edx,cl; mov ecx,[eax+edx*4+20h]; jmp 0x7A664E`
    __asm {
        cmp cl, 6
        jb bd2_stock
        jne bd2_target
        movzx edx, cl
        mov ecx, [eax+20h]
        push g_rs7_resume_BD2
        add dword ptr [esp], 0Eh           // 0x7A664E: after the body's `jmp` (+7, 2 bytes) and `mov ecx,2` (+9, 5)
        ret
    bd2_stock:
        jmp g_rs7_resume_BD2
    bd2_target:
        jmp g_rs7_target_BD2
    }
}

// 0x7A6D80: body `mov ecx,[edi+37Ch]; mov edx,[ecx+24h]; movzx eax,bl; mov edi,[edx+eax*4+20h]; jmp 0x7A6D9C`
RS7_STUB(Rs7_Stub_BD3) {
    __asm {
        cmp bl, 6
        jb bd3_stock
        jne bd3_target
        mov ecx, [edi+37Ch]
        mov edx, [ecx+24h]
        movzx eax, bl
        mov edi, [edx+20h]
        push g_rs7_resume_BD3
        add dword ptr [esp], 17h  // 0x7A6D9C: after the body's `jmp` (+15h, 2 bytes)
        ret
    bd3_stock:
        jmp g_rs7_resume_BD3
    bd3_target:
        jmp g_rs7_target_BD3
    }
}

// 0x7A764D: body `mov edx,[eax+24h]; movzx ecx,bl; mov eax,[edx+ecx*4+20h]; mov [ebp-18h],eax; jmp 0x7A7672`
RS7_STUB(Rs7_Stub_BD4) {
    __asm {
        cmp bl, 6
        jb bd4_stock
        jne bd4_target
        mov edx, [eax+24h]
        movzx ecx, bl
        mov eax, [edx+20h]
        mov [ebp-18h], eax
        push g_rs7_resume_BD4
        add dword ptr [esp], 20h  // 0x7A7672 = 0x7A7652 + 20h
        ret
    bd4_stock:
        jmp g_rs7_resume_BD4
    bd4_target:
        jmp g_rs7_target_BD4
    }
}

// D2/D3 now use C++ mid-hook callbacks (mid_examples.cpp).

// D5 - Rnd_NoteGems_UpdatePositions join (0x7A4E5C), diagnostics only. EBX = gem (+0Ch string),
// [esp+1Ch] = the pre-offset position vec3. Logs strings 0 and 6, then replays
// `fld dword ptr [ebx+14h]; mov eax,[esp+10h]`. Flags may be live: the compares sit inside
// pushfd/popfd.
RS7_STUB(Rs7_Stub_D5) {
    __asm {
        pushfd
        cmp byte ptr [ebx+0Ch], 6
        je d5_log
        cmp byte ptr [ebx+0Ch], 0
        jne d5_out
    d5_log:
        pushad
        lea eax, [esp+36+1Ch]
        push eax
        push ebx
        call Rs7_DiagGemPos
        add esp, 8
        popad
    d5_out:
        popfd
        fld dword ptr [ebx+14h]
        mov eax, [esp+10h]
        jmp g_rs7_resume_D5
    }
}

// ---- The tuner for 7-string arrangements (core/tuner.h; docs/ARCHITECTURE.md) ----
// Every stub's stock path is one global read (g_rs7_tuner_any == 0) and the replayed bytes.

RS7_VAR(g_rs7_resume_W);
RS7_VAR(g_rs7_resume_AT);

// TK and TS (tuner session entries) use C++ mid-hook callbacks (mid_cold.cpp).

// W - `fcomp float [1224C6Ch]` (6 bytes) of UI_PlayerTuner_CheckWrongInstrument 0x5B2DDE, ST0 = the
// mean of ten detected notes, ESI = session. With [tuner] lowrange=1 and a tuner state existing, the
// threshold is the session's (Rs7_WrongThreshold writes g_rs7_wrong_thr; integer/SSE only, no x87);
// otherwise the stock 34.0. Same single pop and the same status word as stock; EFLAGS are rewritten
// by the `test ah,41h` at the resume address, so none are kept.
RS7_STUB(Rs7_Stub_W) {
    __asm {
        cmp g_rs7_lowrange, 0
        je w_stock
        cmp g_rs7_tuner_any, 0
        je w_stock
        pushad
        push esi
        call Rs7_WrongThreshold
        add esp, 4
        popad
        fcomp dword ptr [g_rs7_wrong_thr]
        jmp g_rs7_resume_W
    w_stock:
        fcomp dword ptr [g_rs7_wrong_stock]
        jmp g_rs7_resume_W
    }
}

// AT - entry of UI_PlayerTuner_IsStringTuned 0x5B0C20 (ESI = session; 3 args, ret 0Ch): with
// [debug] tuner_autotune=N, "tuned" N seconds after the selection (test driving only).
RS7_STUB(Rs7_Stub_AT) {
    __asm {
        cmp g_rs7_tuner_auto, 0
        je at_stock
        pushad
        push esi
        call Rs7_TunerAutotune
        add esp, 4
        test eax, eax
        popad
        jz at_stock
        mov al, 1
        ret 0Ch
    at_stock:
        push ebp
        mov ebp, esp
        push ecx
        cmp byte ptr [esi+98h], 0
        jmp g_rs7_resume_AT
    }
}

// AL1 - entry of the nif memory handler Res_NifMemHandler 0x4A8560 (cdecl (name, buf, size, ctx)
// -> resource; called through the handler map from Res_ParseFileJob_Load 0x4A7AB0 on a loader
// thread). Flags are dead at a function entry. [assets] headstock7=1: tail-jump into the cdecl C
// wrapper Rs7_NifHandler (same signature; it calls the stock body through Rs7_NifHandlerStock).
// Otherwise the replay `push ebp; mov ebp,esp; mov eax,[0x135FBFC]` (the X_DT global) and back.
RS7_STUB(Rs7_Stub_AL1) {
    __asm {
        cmp dword ptr [g_rs7_asset_on], 0
        jne al1_ours
        push ebp
        mov ebp, esp
        mov eax, dword ptr [g_rs7_frame_global]
        mov eax, dword ptr [eax]
        jmp g_rs7_resume_AL1
    al1_ours:
        jmp Rs7_NifHandler
    }
}

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

// ---- Virtual guitarist ([test] vguitar=1 only: Rs7_Startup writes these four sites only then;
// core/vguit.h, docs/ARCHITECTURE.md). Each stub calls its cdecl C entry with the
// whole register file (pushad), the x87/SSE state (fxsave into a 16-byte aligned block, fninit for
// an empty x87 stack) and the flags saved, then replays the overwritten instructions. g_rs7_vg_on = 0
// (a fault switched the mode off) skips the call. In the pushad frame [esi]: +4 ESI, +10h EBX,
// +14h EDX, +18h ECX, +20h EFLAGS, +24h the site's [esp].
RS7_VAR(g_rs7_resume_TICK);
RS7_VAR(g_rs7_resume_JUD1);
RS7_VAR(g_rs7_resume_JUD2);
RS7_VAR(g_rs7_resume_AUD1);

#define RS7_VG_ENTER                                                                                                   \
    __asm pushfd __asm pushad __asm mov esi, esp __asm sub esp, 512 __asm and esp, -16 __asm fxsave[esp] __asm fninit
#define RS7_VG_LEAVE __asm fxrstor[esp] __asm mov esp, esi __asm popad __asm popfd

// TICK - entry of Play_Session_UpdateNoteDetection 0x7E2880 ([esp+4] session, [esp+8] float song
// time): Rs7Vg_OnTick(session, time), then the replay `push ebp; mov ebp,esp; and esp,-40h`.
// The site's [esp+4] / [esp+8] are at [esi+28h] / [esi+2Ch].
RS7_STUB(Rs7_Stub_TICK) {
    __asm {
        cmp g_rs7_vg_on, 0
        je tick_stock
    }

    RS7_VG_ENTER
    __asm {
        push dword ptr [esi+2Ch]
        push dword ptr [esi+28h]
        call Rs7Vg_OnTick
        add esp, 8
    }
    RS7_VG_LEAVE
    __asm {
    tick_stock:
        push ebp
        mov ebp, esp
        and esp, -40h
        jmp g_rs7_resume_TICK
    }
}

// JUD1 - 0x7E2CFF in 0x7E2880 (EBX = note object, ESI = session; flags dead, the next instruction
// is a cmp): Rs7Vg_OnJudge(note, session), then the replay `mov byte ptr [ebx+0C3h],1`.
RS7_STUB(Rs7_Stub_JUD1) {
    __asm {
        cmp g_rs7_vg_on, 0
        je jud1_stock
    }

    RS7_VG_ENTER
    __asm {
        push dword ptr [esi+4]  // ESI = session
        push dword ptr [esi+10h]  // EBX = note object
        call Rs7Vg_OnJudge
        add esp, 8
    }
    RS7_VG_LEAVE
    __asm {
    jud1_stock:
        mov byte ptr [ebx+0C3h], 1
        jmp g_rs7_resume_JUD1
    }
}

// JUD2 - entry of Play_Session_ScoreNote 0x7E6AD0 (CL = hit, EDX = session, [esp+4] = note object):
// Rs7Vg_OnScore(hit, session, note), then the replay `push ebp; mov ebp,esp; and esp,-40h`.
RS7_STUB(Rs7_Stub_JUD2) {
    __asm {
        cmp g_rs7_vg_on, 0
        je jud2_stock
    }

    RS7_VG_ENTER
    __asm {
        push dword ptr [esi+28h]  // note ([esp+4] of the caller)
        push dword ptr [esi+14h]  // EDX = session
        movzx eax, byte ptr [esi+18h]  // CL
        push eax
        call Rs7Vg_OnScore
        add esp, 0Ch
    }
    RS7_VG_LEAVE
    __asm {
    jud2_stock:
        push ebp
        mov ebp, esp
        and esp, -40h
        jmp g_rs7_resume_JUD2
    }
}

// AUD1 - 0xC4618C in SndIn_PA_Read 0xC460C0 (ESI = device, EBX = AkAudioBuffer; Wwise audio thread),
// after the FIFO copy, before the detection callback dev+4: Rs7Vg_OnInput(dev, buf) writes the
// synthesized samples, then the replay `mov eax,[esi+4]; test eax,eax` (its flags feed the jz at
// the resume address 0xC46191).
RS7_STUB(Rs7_Stub_AUD1) {
    __asm {
        cmp g_rs7_vg_on, 0
        je aud1_stock
    }

    RS7_VG_ENTER
    __asm {
        push dword ptr [esi+10h]  // EBX
        push dword ptr [esi+4]  // ESI
        call Rs7Vg_OnInput
        add esp, 8
    }
    RS7_VG_LEAVE
    __asm {
    aud1_stock:
        mov eax, [esi+4]
        test eax, eax
        jmp g_rs7_resume_AUD1
    }
}

// ---- chords with the 7th string: the chord frame's string-axis span (B1/B2) and the string-6 finger
// marker (FN2/FN4). Everything else is the native N-slot chord path (core/nslot.h, stubs_nslot.cpp).
RS7_VAR(g_rs7_resume_B1);
RS7_VAR(g_rs7_target_B1);
RS7_VAR(g_rs7_target_B2);
RS7_VAR(g_rs7_fn_sst);

// B1 - 0x7A2BCA in the chord box builder 0x7A2500, after Rnd_GemGeom_Ctor of the frame (mode 4):
// EAX = the geometry component (or 0), [EBP+8] = chord render object, [EBP+0Ch] = the highway (guitar at +37Ch).
// Rs7Chord_OnBoxGeom marks the component of every chord frame of a 7/8-string arrangement; then the replay `mov
// esi,[ebp+18h]; push eax; call 0x7A88B0` (stdcall, one argument).
RS7_STUB(Rs7_Stub_B1) {
    __asm {
        pushad
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+8]
        push eax
        call Rs7Chord_OnBoxGeom
        add esp, 12
        popad
        mov esi, [ebp+18h]
        push eax
        call g_rs7_target_B1
        jmp g_rs7_resume_B1
    }
}

// B2 - the call of Rnd_GemGeom_ApplySpan 0x7AEE80 at 0x7A91E4 in Rnd_GemGeom_Build (ECX = component
// +20h vec3, EDX = +2Ch vec3, [esp+4] = the model entity, caller cleans; EDI = component; x87 empty).
// A component B1 marked gets the stock span scale/translation plus the frame's string-axis scale
// (Rs7Chord_BoxScale fills g_rs7_box_scale / g_rs7_box_trans), set with the stock
// Rnd_Gem_SetScaleTranslate 0x7AFC00 (EAX = entity; X_SST) and the entity's vfunc +8, exactly as
// ApplySpan does. Everything else: stock ApplySpan.
extern "C" __declspec(dllexport) float g_rs7_box_scale[3] = {1.0f, 1.0f, 1.0f};
extern "C" __declspec(dllexport) float g_rs7_box_trans[3] = {0.0f, 0.0f, 0.0f};
RS7_STUB(Rs7_Stub_B2) {
    __asm {
        pushad
        push edi
        call Rs7Chord_BoxScale
        add esp, 4
        test eax, eax
        popad
        jz b2_stock
        mov eax, [esp+4]
        test eax, eax
        jz b2_done
        push offset g_rs7_box_trans
        push offset g_rs7_box_scale
        call g_rs7_fn_sst
        add esp, 8
        mov ecx, [esp+4]
        mov eax, [ecx]
        call dword ptr [eax+8]
    b2_done:
        ret
    b2_stock:
        jmp g_rs7_target_B2
    }
}

// ---- Seven-string tuning names (core/tuningname.h; GitHub issue #15) ----
// Stock path: one global read (g_rs7_tn_on == 0) and the replayed prologue. With the switch on the
// C side decides per call; anything that is not a 7-string tuning runs the original alone.

RS7_VAR(g_rs7_resume_TJ);
RS7_VAR(g_rs7_resume_TU);
RS7_VAR(g_rs7_resume_TN);

// TJ - entry of the manifest tuning-name resolver 0x56C950 (ECX = raw out, EDX = display out,
// [esp+4] = owned Tuning JSON, [esp+8] = CentOffset; caller cleans; void). A 7-string Tuning:
// Rs7_TnJsonPre reads it (the original releases it), the original runs with the same registers
// and a copy of its two arguments, Rs7_TnJsonPost assigns the outputs.
RS7_STUB(Rs7_Stub_TJ) {
    __asm {
        cmp g_rs7_tn_on, 0
        je tj_stock
        pushad
        push dword ptr [esp+28h]  // CentOffset
        push dword ptr [esp+28h]  // Tuning JSON
        push edx
        push ecx
        call Rs7_TnJsonPre
        add esp, 10h
        test eax, eax
        jz tj_stock_pop
        mov [esp+1Ch], eax  // the handle comes back in EAX (dead at the entry: the cookie load)
        popad
        push eax  // [esp] handle, [esp+4] return, [esp+8] JSON, [esp+0Ch] cents
        push dword ptr [esp+0Ch]
        push dword ptr [esp+0Ch]
        call tj_stock
        add esp, 8
        pushad
        push dword ptr [esp+20h]
        call Rs7_TnJsonPost
        add esp, 4
        popad
        add esp, 4
        ret
    tj_stock_pop:
        popad
    tj_stock:
        push ebp
        mov ebp, esp
        sub esp, 11Ch
        jmp g_rs7_resume_TJ
    }
}

// TU - entry of the arrangement-UUID resolver 0x56C300 (ECX = UUID*, EDX = output; EAX = output;
// no stack arguments). The original runs as a call between Rs7_TnUuidPre and Rs7_TnUuidPost; TUT
// tells the open call which Tuning it found.
RS7_STUB(Rs7_Stub_TU) {
    __asm {
        cmp g_rs7_tn_on, 0
        je tu_stock
        pushad
        push edx
        call Rs7_TnUuidPre
        add esp, 4
        test eax, eax
        jz tu_stock_pop
        mov [esp+1Ch], eax
        popad
        push eax  // [esp] handle, [esp+4] return
        call tu_stock
        pushad
        push dword ptr [esp+20h]
        call Rs7_TnUuidPost
        add esp, 4
        popad
        add esp, 4
        ret
    tu_stock_pop:
        popad
    tu_stock:
        push ebp
        mov ebp, esp
        sub esp, 10Ch
        jmp g_rs7_resume_TU
    }
}

// TUT (0x56C408, the open call's Tuning object) uses a C++ mid-hook callback (mid_cold.cpp).

// TN - entry of the tuner's array resolver 0x5B7750 (cdecl: [esp+4] = output string, [esp+8] =
// int[6]; EAX = output). Rs7_TnArrayPre constructs a known 7-string name itself (the original is
// skipped); an unknown 7-string tuning runs the original and Rs7_TnArrayPost marks its result.
RS7_STUB(Rs7_Stub_TN) {
    __asm {
        cmp g_rs7_tn_on, 0
        je tn_stock
        push 0  // post flag
        pushad  // [esp+20h] post, [esp+24h] return, [esp+28h] out, [esp+2Ch] offsets
        lea eax, [esp+20h]
        push eax
        push dword ptr [esp+30h]
        push dword ptr [esp+30h]
        call Rs7_TnArrayPre
        add esp, 0Ch
        test eax, eax
        jnz tn_done
        cmp dword ptr [esp+20h], 0
        jne tn_wrap
        popad
        add esp, 4
        jmp tn_stock
    tn_done:
        popad
        add esp, 4
        mov eax, [esp+4]
        ret
    tn_wrap:
        popad  // [esp] post, [esp+4] return, [esp+8] out, [esp+0Ch] offsets
        push dword ptr [esp+0Ch]
        push dword ptr [esp+0Ch]
        call tn_stock
        add esp, 8
        pushad
        push dword ptr [esp+20h]
        push dword ptr [esp+2Ch]
        call Rs7_TnArrayPost
        add esp, 8
        popad
        add esp, 4
        ret
    tn_stock:
        push ebp
        mov ebp, esp
        sub esp, 0CCh
        jmp g_rs7_resume_TN
    }
}

// SE1/SE2/SE3 use C++ context callbacks in mid_songend.cpp.

// XB1 (0x55922C, end of SNG_Song_Load: reads the RS7X extension block, issue #18) uses a C++ mid-hook
// callback (mid_cold.cpp); the two stock instructions (`lea ecx,[esp+14h]; call 0x9D8B10`) are replayed
// by the thunk.
