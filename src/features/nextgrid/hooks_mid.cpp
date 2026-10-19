// Mid-hooks of the native next-note grid (nextgrid.h): NG1 (the cell index of Rnd_NextNoteGrid_Set over 8 rows, light:
// per note set, integer only) and NG9 (the string-6 neck targets and the tree release after the consumer loop, full
// mode: once per frame, its helpers log). Converted from naked stubs (refactor step 7 batch 5, frozen at 9c0e44f,
// tools/tests/test_midhook_features.py). Both skip the replay and do the replaced instructions' work themselves: the
// displaced bytes carry the stock offsets of a frame and a grid that grew. NGP (a stack probe) stays a stub
// (hooks_stubs.cpp).
#include "features/nextgrid/nextgrid.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_NG1 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_NG9 = 0, g_rs7_target_NG9 = 0;
}

// NG1 - 0x7AA166 in Rnd_NextNoteGrid_Set 0x7AA140 (ESI = grid, [ebp+8] string byte, [ebp+0Ch] fret byte, ST0 = the new
// time; the time-set insert already ran). Replaces `movzx ecx,[ebp+8]; movzx edx,[ebp+0Ch]; lea eax,[ecx+1Ah]; imul
// eax,eax,1Ah; add eax,edx` (16 bytes); stock continues with `lea edi,[esi+eax*4]` = &time cell. EAX = s*26 + f + 347h
// puts it at G + 0D1Ch + (s*26+f)*4; ECX = s and EDX = f are the stock values for the pointer store at 0x7AA184. s >= 8
// or f >= 26: the stock reject tail 0x7AA198 (resume + 22h: `fstp st(0); pop edi; mov esp,ebp; pop ebp; ret 10h`),
// nothing stored. The old stub left the flags of its compare / add; none is read before they are rewritten.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_NG1(Context& c) {
    const uint32_t s = Rs7Mid::Byte(c.ebp + 8);
    const uint32_t f = Rs7Mid::Byte(c.ebp + 0x0C);

    c.ecx = s;
    c.edx = f;

    if (!rs7ng::SetAccepts(s, f)) {
        c.jump = static_cast<uint32_t>(g_rs7_resume_NG1) + 0x22;
        return;
    }

    c.eax = s * rs7ng::kCells + f + rs7ng::kSetEaxBias;
    c.jump = static_cast<uint32_t>(g_rs7_resume_NG1);
}

// NG9 - 0x7E49D6 in Rnd_NeckTargets_Update 0x7E4870, right after the consumer loop (FPU empty, no XMM in the function).
// Replaces the tree release `cmp dword [ebp-0Ch],0; pop edi; pop esi; je 0x7E49EA; mov edx,[ebp-18h]; push edx; lea
// ecx,[ebp-1Ch]` (15 bytes; the `call 0x7D06C0` follows) with the native frame's offsets: grid ebp-1060h, EBX = the
// renderer, dt [ebp+0Ch], t [ebp-106Ch], fret count [ebp-1070h], active string count [ebp-1074h]; the tree's size /
// root / header are ebp-34Ch / -358h / -35Ch. Rs7_NeckRow6N spawns string 6 while the active count is 6 (and only reads
// row 6 when it is empty), Rs7_NeckLog once per song. Flags: the tree-size compare's, as stock.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_NG9(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    const int fret = Rs7_NeckRow6N(Rs7Mid::Ptr(c.ebp - 0x1060), Rs7Mid::Ptr<void>(c.ebx), Dword(c.ebp + 0x0C),
                                   Dword(c.ebp - 0x106C), Dword(c.ebp - 0x1070), Dword(c.ebp - 0x1074));

    if (fret >= 0) {
        Rs7_NeckLog(fret);
    }

    const uint32_t treeSize = Dword(c.ebp - 0x34C);

    c.Compare32(treeSize, 0);
    c.edi = Dword(c.esp);
    c.esi = Dword(c.esp + 4);
    c.esp += 8;

    if (!treeSize) {
        c.jump = static_cast<uint32_t>(g_rs7_target_NG9);
        return;
    }

    c.edx = Dword(c.ebp - 0x358);
    Rs7Mid::Push(c, c.edx);
    c.ecx = c.ebp - 0x35C;
    c.jump = static_cast<uint32_t>(g_rs7_resume_NG9);
}
