// Chord-diagram data stubs ([chords] diagrams7, core/diagrams.h, docs/ARCHITECTURE.md section 9): the
// stock diagram serializers run, then C++ adds the LowB / LowFs data keys for the 7th / 8th string row.
// All four stubs only call into C++; diagrams7 gates every write inside it.
//   DGB  bind the PostProcess attribute dictionary to its SNG song (Rs7Diagram_Bind)
//   DGM  after the colour/handedness finalizer: add the keys (Rs7Diagram_Manifest)
//   DGS  after the loaded-song serializer: add the keys (Rs7Diagram_Loaded)
//   TP13 template lookup of the loaded song through Rs7_NsTplAt (T8 records)
// DGB / DGM / DGS save the full state (RS7_SAVE_ALL: flags, GPRs, x87/SSE) around the C++ call; TP13 only
// calls the integer helper Rs7_NsTplAt.
#include "core/diagrammovie.h"
#include "core/diagrams.h"
#include "hooks/stub_macros.h"

#define STUB(n) RS7_STUB(Rs7_Stub_##n)
extern "C" void Rs7_NsTplAt();
RS7_RESUME_VAR(DGM);
RS7_RESUME_VAR(DGS);
RS7_RESUME_VAR(TP13);
RS7_RESUME_VAR(DGB);

// DGB - bind the attribute dictionary to its SNG song.
// 55A02C: PostProcess's UUID attribute lookup (8966C0) completed. Bind this
// exact dictionary identity to its SNG song; no matching across songs by shape.
STUB(DGB) {
    RS7_SAVE_ALL
    __asm {
        push dword ptr [esi+74h]  // saved ESP + 24h + local attribute holder 50h
        push dword ptr [ebp+8]
        call Rs7Diagram_Bind
        add esp,8
    }
    RS7_RESTORE_ALL
    __asm mov esi, [esp + 50h] __asm lea ecx, [esp + 1b8h] __asm jmp g_rs7_resume_DGB
}

// DGM - add the LowB / LowFs keys after the manifest-side finalizer.
// 757698: colour/handedness finalizer completed; all borrowed arrays/args and
// the newly built dictionary are still alive. ESP includes its unpopped argument.
STUB(DGM) {
    RS7_SAVE_ALL
    __asm {
        lea edx,[esi+24h]
        movzx eax,byte ptr [edx+3fh]
        push eax
        push dword ptr [ebp+10h]
        push dword ptr [ebp+0ch]
        push dword ptr [edx+58h]  // fingers: local +54, plus finalizer argument
        push dword ptr [edx+5ch]  // frets: local +58, plus finalizer argument
        push dword ptr [edx+50h]
        call Rs7Diagram_Manifest
        add esp,24
    }
    RS7_RESTORE_ALL
    __asm add esp, 4 __asm lea ecx, [esp + 74h] __asm jmp g_rs7_resume_DGM
}

// DGS - add the LowB / LowFs keys after the loaded-song serializer.
// 7580DF: all stock fields finished; holder/template arguments are borrowed.
STUB(DGS) {
    RS7_SAVE_ALL
    __asm {
        push dword ptr [ebp+0ch]
        push dword ptr [ebp+8]
        call Rs7Diagram_Loaded
        add esp,8
    }
    RS7_RESTORE_ALL
    __asm mov ecx, [esp + 180h] __asm jmp g_rs7_resume_DGS
}

// TP13 - T8 template for the loaded-song diagram lookup.
// 7578BC..C5: loaded-song UI template address. Keep the stock side effects on
// EDX/ECX, and the caller's existing song retain/release surrounding this block.
STUB(TP13) {
    __asm {
        cmp g_rs7_diagrams,0
        je stock
        push eax
        push edx
        mov edx,eax
        mov eax,[esi+94h]
        call Rs7_NsTplAt
        pop edx
        pop ecx
        lea ecx,[ecx+ecx*8]
        mov edx,[esi+94h]
        jmp g_rs7_resume_TP13
    stock:
        mov edx,[esi+94h]
        lea ecx,[eax+eax*8]
        lea eax,[edx+ecx*8]
        jmp g_rs7_resume_TP13
    }
}
