// The headstock's Wrap mid-hook (ARCHITECTURE.md section 15 "Wrap placement"). Converted from a naked stub (frozen at
// b72bc52, tools/tests/test_midhook_wrap_sites.py).
#include "features/headstock/hooks.h"

extern "C" {
// PSN2 - the call of the named-colour dispatch 0x59BF80 at 0x5B08F6 (EAX = name, ECX = player, [esp+4] = slot or NULL,
// `ret 4`; EBX = peg index, ESI = session, both callee-saved by 0x59BF80), a Wrap site (frozen at b72bc52,
// tools/tests/test_midhook_wrap_sites.py). Before: a NULL slot (index not active / not taken; stock never passes one
// for its six) skips the call instead of dereferencing it, EAX unchanged. After: index >= 6 adds Rs7_PegNamed. Full
// mode: once per tuner selection.
__declspec(dllexport) void __cdecl Rs7_Mid_PSN2Before(Rs7Mid::Context& c) {
    if (Rs7Mid::Frame(c, kRs7WrapPSN2).Arg(0) == 0) {
        Rs7Mid::WrapReturn(c, kRs7WrapPSN2, c.eax);
    }
}

__declspec(dllexport) void __cdecl Rs7_Mid_PSN2(Rs7Mid::Context& c) {
    if (c.ebx >= 6) {
        Rs7_PegNamed(Rs7Mid::Ptr(c.esi), c.ebx);
    }
}
}
