// Mid-hooks of the native FretGrid / GuitarStrings string count (hooks_grid.cpp holds what they call). With [neck]
// native_publish=1 and all native layouts present, InstrumentInfo +0Bh holds the guitar's real string count (7/8) and
// these hooks make the stock grid code follow it: bounds that were a hard 6 read the count, the build covers strings
// 0-7, and the attach-point / finger / edge queries are answered over the native strings. Written only with the
// publication ports (registration.cpp, group Publish).
//   PUBGS PUBGE1 PUBGE2  string-bound guards `cmp reg,6; jae` -> `cmp reg,[info+0Bh]` (light)
//   PUBGC PUBGO          Rnd_FretGrid_Build: stock prefix traversal capped at 6; world columns count 8 (light)
//   PUBGB                Rnd_FretGrid_Build exit: build the extra cells 6/7 once
//   PUBGA                Rnd_GetStringAttachPoint entry: strings 6/7 via Rs7Gs_AttachExt
//   PUBGF                finger span over the template's physical ranks
//   PUBEDGE              the edge string of a string action over N strings (light)
//   PUBGP (site R1)      Rnd_FretGrid_GetCellPos: every cell position query from the native cells (sse)
// Converted from naked stubs (refactor step 7 batch 5, frozen at 9c0e44f, tools/tests/test_midhook_features.py). Light
// where the site runs per cell / per note and the helper is integer only; full where the helper writes floats
// (PUBGA) or the site runs once per build or chord (PUBGB, PUBGF). PUBGP (R1) runs per gem / cell with a helper that
// computes floats: sse mode (frozen at 7f80b7b, tools/tests/test_midhook_sse_sites.py).
#include "features/publish/hooks.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Byte;
using Rs7Mid::Context;
using Rs7Mid::Dword;
using Rs7Mid::Ptr;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_PUBGS = 0, g_rs7_target_PUBGS = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PUBGE1 = 0, g_rs7_target_PUBGE1 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PUBGE2 = 0, g_rs7_target_PUBGE2 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PUBGC = 0, g_rs7_resume_PUBGO = 0, g_rs7_resume_PUBGB = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PUBEDGE = 0;
}

namespace {
uint32_t WithLow8(uint32_t reg, uint32_t low) {
    return (reg & ~0xFFu) | (low & 0xFF);
}

// `cmp string, byte [info+0Bh]; jae target`, falling through to the resume (flags: the compare's, as stock).
void BoundGuard(Context& c, uint32_t string, uint32_t info, uintptr_t resume, uintptr_t target) {
    const uint32_t count = Byte(info + 0x0B);

    c.Compare(string, count, 8);
    c.jump = static_cast<uint32_t>((string & 0xFF) >= count ? target : resume);
}
}  // namespace

// PUBGS 0x5903BD (AL = string, EDI = info), PUBGE1 0x7E3488 (DL, ESI), PUBGE2 0x7E367D (BL, ESI): the stock bound
// `cmp reg,6; jae <reject>` against the info's published count. PUBGS / PUBGE1 displace `cmp; jae rel32` exactly.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGS(Context& c) {
    BoundGuard(c, c.eax, c.edi, g_rs7_resume_PUBGS, g_rs7_target_PUBGS);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGE1(Context& c) {
    BoundGuard(c, c.edx, c.esi, g_rs7_resume_PUBGE1, g_rs7_target_PUBGE1);
}

// PUBGE2 displaces `cmp bl,6; jae 0x7E36E8` (a rel8) and the following `cmp byte [esi+8],0`, whose flags the `je
// 0x7E368A` at the resume reads (`neg ecx` is skipped when the byte is 0). On the accepted path the hook does that
// compare too. Until refactor step 7 batch 5 neither the stub nor the site table did: the `je` read the guard's flags
// (ZF = 0, `neg ecx` always ran) and the site's target was decoded as a rel32 (a branch into 0x86B506 on reject).
// CONFIRMED by a run of the stock bytes (tools/tests/test_publish_grid_emu.py, test_pubge2_matches_the_stock_bytes).
// HYPOTHESIS: InstrumentInfo +8 is a direction flag of the fret offset (ECX) passed to 0x58E6E0.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGE2(Context& c) {
    BoundGuard(c, c.ebx, c.esi, g_rs7_resume_PUBGE2, g_rs7_target_PUBGE2);
    if (c.jump == static_cast<uint32_t>(g_rs7_resume_PUBGE2)) {
        c.Compare(Byte(c.esi + 8), 0, 8);
    }
}

// PUBGC - 0x58F8AF in Rnd_FretGrid_Build 0x58F8A0: `mov cl,[eax+0Bh]; mov dl,[eax+0Ch]` (EAX = info) reads the string
// count for the stock prefix traversal, capped at 6 before either 7/8 publication: the stock loops keep their
// six-string shape, the extra cells are built once at the exit (PUBGB). The old stub's `cmp cl,6` flags are dead (the
// next flag reader follows a flag-writing instruction; contract "free").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGC(Context& c) {
    const uint32_t count = Byte(c.eax + 0x0B);

    c.ecx = WithLow8(c.ecx, count > 6 ? 6 : count);
    c.edx = WithLow8(c.edx, Byte(c.eax + 0x0C));
    c.jump = static_cast<uint32_t>(g_rs7_resume_PUBGC);
}

// PUBGO - 0x58FCF6 `mov dl,[eax+0Bh]; sub esp,24h` (EAX = info): populate all eight world columns before publishing the
// active count N; a guitar (count >= 6) reads 8, bass stays 4. Flags: the `sub esp,24h`'s, as stock.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGO(Context& c) {
    const uint32_t count = Byte(c.eax + 0x0B);

    c.edx = WithLow8(c.edx, count < 6 ? count : 8);
    c.Compare32(c.esp, 0x24);
    c.esp -= 0x24;
    c.jump = static_cast<uint32_t>(g_rs7_resume_PUBGO);
}

// PUBGB - 0x58FA25 `mov eax,2` (the "complete" exit of Rnd_FretGrid_Build; [ebp+8] = grid, [ebp+0Ch] = guitar):
// Rs7Publish_GridBuild builds the cells of strings 6/7 and returns the status (EAX; stock 2, or "not ready" while the
// guitar's strings are still loading). The old stub left the helper's flags (contract "dead").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGB(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    c.eax = static_cast<uint32_t>(Rs7Publish_GridBuild(Ptr(Dword(c.ebp + 8)), Ptr(Dword(c.ebp + 0x0C))));
    c.jump = static_cast<uint32_t>(g_rs7_resume_PUBGB);
}

// PUBGA - Rnd_GetStringAttachPoint 0x594530 (ECX = info, BYTE string, float* out; caller-cleaned). Strings 0..5 keep
// the exact stock entry (the replayed `push ebp; mov ebp,esp; sub esp,4Ch`); 6/7 go to Rs7Gs_AttachExt, anything above
// returns 0. Only the low byte of the string argument may be compared: Rnd_GuitarStrings_Load pushes a whole stack
// dword (0x59A5DD `mov edx,[esp+14h]`) whose upper three bytes are stale (a dword compare sent string 0 to `invalid`,
// the load stayed pending forever and no song could start: CONFIRMED, tests/pubfix run5). Flags after a skipped entry
// are dead (a return); the old stub left its own (contract "free").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGA(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    const uint32_t s = Byte(c.esp + 4);

    if (s < 6) {
        return;
    }

    const uint32_t point =
        s < 8 ? static_cast<uint32_t>(Rs7Gs_AttachExt(Ptr(c.ecx), Dword(c.esp + 4), Ptr<float>(Dword(c.esp + 8)))) : 0;

    Rs7Mid::Return(c, point);
}

// PUBGF - 0x58CF90, entry of the finger-span function (stdcall, `ret 8`: argument 1 = the template, 2 = the span out
// pointer, EDI = the finger): with the publication ports on Rs7Publish_FingerSpan computes the span over the template's
// physical ranks (native T8 records) or with the exact stock scan (stock templates). Off: the replayed `push ebp; mov
// ebp,esp; push ebx; push esi` and the stock body. The old stub left the flags of its switch test or of the helper;
// none is read before it is rewritten (contract "free").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGF(Context& c) {
    if (!g_rs7_native_publish) {
        return;
    }

    const int span = Rs7Publish_FingerSpan(Ptr<const uint8_t>(Dword(c.esp + 4)), c.edi, Ptr<int>(Dword(c.esp + 8)));

    Rs7Mid::Return(c, static_cast<uint32_t>(span), 8);
}

// PUBEDGE - 0x7E72D0 in the string-action function 0x7E7140 (EAX = info, CL = first): the stock edge string `first ? 0
// : count-1` (13 bytes: test cl,cl / xor al,al / mov al,[eax+0Bh] / dec al) becomes Rs7Publish_Edge (physical rank of
// the first / last string of the published count). EAX = the edge string. The old stub left the helper's flags
// (contract "dead").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBEDGE(Context& c) {
    c.eax = Rs7Publish_Edge(Ptr(c.eax), c.ecx & 0xFF);
    c.jump = static_cast<uint32_t>(g_rs7_resume_PUBEDGE);
}

// PUBGP - the entry of Rnd_FretGrid_GetCellPos 0x58FE00 (site R1; AL = fret, ECX = out, stack: grid, k, string; `ret
// 0Ch`), replaced whole: every query is answered from the native cells, strings 0-7, no extrapolation
// (Rs7Publish_GridPos, SSE float math). EAX = its result, the other registers as at the call.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBGP(Context& c) {
    const uint32_t grid = Dword(c.esp + 4), k = Dword(c.esp + 8), string = Dword(c.esp + 12);

    c.eax = uint32_t(Rs7Publish_GridPos(Ptr(grid), c.eax, k, string, Ptr<float>(c.ecx)));
    Rs7Mid::Ret(c, 12);
}
