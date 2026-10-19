// Mid-hook callbacks of the plain-PSARC unit (psarc.h): the plain TOC (PP_TOC) and the plain SNG flags (PP_SNGF). Both
// are jmp_atomic8 sites written by the unit's own compare-exchange next to the CDLC enabler (hooks.cpp), entries built
// by Rs7_PrepareMid (the whole `expect` span is the replay). Cold: once per archive / SNG load, full mode. Converted
// from naked stubs (frozen at fdfddcf, tools/tests/test_midhook_asm_sites3.py); the old stubs' compare / test flags are
// reproduced (Context::Test, Compare32). PP_SNGF assumes the stock `jne <reject>` at site + 5..+10: the entry replays
// it from the site's `expect` bytes, not from the live code.
#include "features/psarc/psarc.h"
#include "platform/midhook.h"
#include <cstdint>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_skip_PP_TOC = 0;     // site + 0x4D: lea ecx,[edi+34h], after the decrypt block
__declspec(dllexport) uintptr_t g_rs7_resume_PP_SNGF = 0;  // site + 5: the stock jne <reject>

// PP_TOC 0x8A3643 `mov ecx,10h` at the start of the TOC decrypt block (`mov eax,[ebp-74h]` before it has run).
// [ebp-54h] = archiveFlags, bit 4 (2) = encrypted TOC: then the replay (the stock decrypt). Plain: what the block's
// tail does without its pushes (EDX = [ebp-74h], EDI = [EDX]), continuing after the block. Flags: those of the old
// stub's `test byte [ebp-54h],4` at both exits.
__declspec(dllexport) void __cdecl Rs7_Mid_PP_TOC(Rs7Mid::Context& c) {
    const uint8_t flags = Rs7Mid::Byte(c.ebp - 0x54);

    c.Test(flags, 4, 8);
    if (flags & 4) {
        return;
    }

    c.edx = Rs7Mid::Dword(c.ebp - 0x74);
    c.edi = Rs7Mid::Dword(c.edx);
    c.jump = uint32_t(g_rs7_skip_PP_TOC);
}

// PP_SNGF 0x558C1A `cmp dword [esp+68h],3` (SNG flags) before the stock `jne <reject>`. Flags 1 (plain) set the flags
// of an equal compare, so the stock jne at site + 5 falls through to the signature check; anything else replays the
// stock compare (and its jne).
__declspec(dllexport) void __cdecl Rs7_Mid_PP_SNGF(Rs7Mid::Context& c) {
    if (Rs7Mid::Dword(c.esp + 0x68) != 1) {
        return;
    }

    c.Compare32(3, 3);
    c.jump = uint32_t(g_rs7_resume_PP_SNGF);
}
}
