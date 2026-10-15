// Publication stubs of the native FretGrid / GuitarStrings string count (core/publish_grid.h), part 1: the
// grid itself. With [neck] native_publish=1 and all native layouts present, InstrumentInfo +0Bh holds the
// guitar's real string count (7/8) and these stubs make the stock grid code follow it: bounds that were a
// hard 6 read the count, the build covers strings 0-7, and the cell / finger / attach-point queries are
// answered from the native cells. Written only with the publication ports (registry: kRs7Publish).
//
// Contents, in file order (sites.json ids; stock addresses in each header):
//   PUBGS PUBGE1 PUBGE2  string-bound guards `cmp reg,6; jae` -> `cmp reg,[info+0Bh]`
//   PUBGC PUBGO          Rnd_FretGrid_Build: stock prefix traversal capped at 6; world columns count 8
//   PUBGB                Rnd_FretGrid_Build exit: build the extra cells 6/7 once
//   PUBGP                Rnd_FretGrid_GetCellPos entry (site R1): cell position from the native cells
//   PUBGA                Rnd_GetStringAttachPoint entry: strings 6/7 via Rs7Gs_AttachExt
//   PUBGF PUBEDGE        finger span and edge string over N strings
#include "core/gsnative.h"
#include "core/publish_grid.h"
#include "hooks/stub_macros.h"

RS7_RESUME_VAR(PUBGS);
RS7_RESUME_VAR(PUBGE1);
RS7_RESUME_VAR(PUBGE2);
RS7_RESUME_VAR(PUBGC);
RS7_RESUME_VAR(PUBGB);
RS7_RESUME_VAR(PUBGA);
RS7_RESUME_VAR(PUBGF);
RS7_RESUME_VAR(PUBEDGE);
RS7_TARGET_VAR(PUBGS);
RS7_TARGET_VAR(PUBGE1);
RS7_TARGET_VAR(PUBGE2);

// PUBGS 0x5903BD / PUBGE1 0x7E3488 / PUBGE2 0x7E367D - string bound of the grid readers: stock
// `cmp al|dl|bl, 6; jae <reject>` (the displaced bytes end with the rel32 / rel8 of the jae) becomes
// the same compare against the info's published count (AL / DL / BL = string, EDI / ESI = info).
RS7_BOUND_GUARD(PUBGS, al, edi)
RS7_BOUND_GUARD(PUBGE1, dl, esi)
RS7_BOUND_GUARD(PUBGE2, bl, esi)

// PUBGC - 0x58F8AF in Rnd_FretGrid_Build 0x58F8A0: `mov cl,[eax+0Bh]; mov dl,[eax+0Ch]` (EAX = info) reads
// the string count for the stock prefix traversal. Capped at 6 before either 7/8 publication: the stock
// loops keep their six-string shape, the extra cells are built once at the exit (PUBGB).
RS7_STUB(Rs7_Stub_PUBGC) {
    __asm {
        mov cl,[eax+0Bh]
        cmp cl,6
        jbe stock_count
        mov cl,6
    stock_count:
        mov dl,[eax+0Ch]
        jmp g_rs7_resume_PUBGC
    }
}

// PUBGB - 0x58FA25 `mov eax,2` (the "complete" exit of Rnd_FretGrid_Build; [ebp+8] = grid, [ebp+0Ch] =
// guitar): Rs7Publish_GridBuild builds the cells of strings 6/7 and returns the status (EAX; stock 2, or
// "not ready" while the guitar's strings are still loading, see GridBuild's bounded wait).
RS7_STUB(Rs7_Stub_PUBGB) {
    __asm {
        pushad
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+8]
        call Rs7Publish_GridBuild
        add esp,8
        mov [esp+28],eax
        popad
        jmp g_rs7_resume_PUBGB
    }
}

// PUBGP - Rnd_FretGrid_GetCellPos 0x58FE00 (site R1, entry replaced; AL = fret, ECX = out, stack: grid, k,
// string; `ret 0Ch`): every query is answered from the native cells, strings 0-7, no extrapolation
// (Rs7Publish_GridPos). The return value is EAX.
RS7_STUB(Rs7_Stub_PUBGP) {
    __asm {
        pushad
        mov ebp,esp
        push dword ptr [ebp+24]
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        push dword ptr [ebp+28]
        push dword ptr [ebp+36]
        call Rs7Publish_GridPos
        add esp,20
        mov [ebp+28],eax
        popad
        ret 0Ch
    }
}

// PUBGA - Rnd_GetStringAttachPoint 0x594530 (this = info in ECX, BYTE string, float* out, caller-cleaned).
// Strings 0..5 keep the exact stock entry (the replayed `push ebp; mov ebp,esp; sub esp,4Ch`); 6/7 go to
// Rs7Gs_AttachExt, anything above returns 0. The string is a byte: Rnd_GuitarStrings_Load pushes a whole
// stack dword (0x59A5DD `mov edx,[esp+14h]`) whose upper three bytes are stale, so only the low byte may
// be compared (a dword compare sent string 0 to `invalid`, the load stayed pending forever and no song
// could start: CONFIRMED, tests/pubfix run5 - all 16 string objects kept the default position
// 1000,-1000,1000 while Rnd_GuitarStrings_Load returned 1 every frame).
RS7_STUB(Rs7_Stub_PUBGA) {
    __asm {
        cmp byte ptr [esp+4],6
        jb stock
        cmp byte ptr [esp+4],8
        jae invalid
        pushad
        mov ebp,esp
        push dword ptr [ebp+40]
        push dword ptr [ebp+36]
        push ecx
        call Rs7Gs_AttachExt
        add esp,12
        mov [ebp+28],eax
        popad
        ret
    invalid:
        xor eax,eax
        ret
    stock:
        push ebp
        mov ebp,esp
        sub esp,4Ch
        jmp g_rs7_resume_PUBGA
    }
}

// PUBGF - 0x58CF90, entry of the finger-span function (stdcall, `ret 8`: stack argument 1 = the template,
// argument 2 = the span out pointer, EDI = the finger): with the publication ports on,
// Rs7Publish_FingerSpan computes the span over the template's physical ranks (native T8 records) or with
// the exact stock scan (stock templates). Off: the replayed `push ebp; mov ebp,esp; push ebx; push esi`
// and the stock body.
RS7_STUB(Rs7_Stub_PUBGF) {
    __asm {
        cmp dword ptr [g_rs7_native_publish],0
        je stock
        pushad
        mov ebp,esp
        push dword ptr [ebp+40]
        push edi
        push dword ptr [ebp+36]
        call Rs7Publish_FingerSpan
        add esp,12
        mov [ebp+28],eax
        popad
        ret 8
    stock:
        push ebp
        mov ebp,esp
        push ebx
        push esi
        jmp g_rs7_resume_PUBGF
    }
}

// PUBEDGE - 0x7E72D0 in the string-action function 0x7E7140 (EAX = info, CL = first): the stock edge string
// `first ? 0 : count-1` (13 bytes: test cl,cl / xor al,al / mov al,[eax+0Bh] / dec al) becomes
// Rs7Publish_Edge (physical rank of the first / last string of the published count). AL = the edge string.
RS7_STUB(Rs7_Stub_PUBEDGE) {
    __asm {
        pushad
        movzx ecx,cl
        push ecx
        push eax
        call Rs7Publish_Edge
        add esp,8
        mov [esp+28],eax
        popad
        jmp g_rs7_resume_PUBEDGE
    }
}

RS7_RESUME_VAR(PUBGO);

// PUBGO - 0x58FCF6 `mov dl,[eax+0Bh]; sub esp,24h` (EAX = info): populate all eight world columns before
// publishing the active count N; a guitar (count >= 6) reads 8, bass stays 4.
RS7_STUB(Rs7_Stub_PUBGO) {
    __asm {
        mov dl,[eax+0Bh]
        cmp dl,6
        jb bass
        mov dl,8
    bass:
        sub esp,24h
        jmp g_rs7_resume_PUBGO
    }
}
