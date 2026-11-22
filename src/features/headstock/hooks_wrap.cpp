// The headstock's Wrap mid-hooks (ARCHITECTURE.md section 14 "Wrap placement").
#include "features/headstock/hooks.h"

extern "C" {
// PSN2 - the call of the named-colour dispatch 0x59BF80 at 0x5B08F6 (EAX = name, ECX = player, [esp+4] = slot or NULL,
// `ret 4`; EBX = peg index, ESI = session, both callee-saved by 0x59BF80), a Wrap site (frozen at b72bc52,
// tools/tests/test_midhook_wrap_sites.py). Before: a NULL slot (index not active / not taken; stock never passes one
// for its six) skips the call instead of dereferencing it, EAX unchanged. After: index >= 6 adds Rsxr_PegNamed. Full
// mode: once per tuner selection.
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN2Before(RsxrMid::Context& c) {
    if (RsxrMid::Frame(c, kRsxrWrapPSN2).Arg(0) == 0) {
        RsxrMid::WrapReturn(c, kRsxrWrapPSN2, c.eax);
    }
}

// PSN3 - the same dispatch call at 0x5B09D2 in the tuner's per-peg loop 0x5B0960 (EBX = peg index): a NULL slot
// (a peg the model has no Locator_Peg for, extended bass strings 4/5) skips the call. No after part.
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN3Before(RsxrMid::Context& c) {
    if (RsxrMid::Frame(c, kRsxrWrapPSN3).Arg(0) == 0) {
        RsxrMid::WrapReturn(c, kRsxrWrapPSN3, c.eax);
    }
}

__declspec(dllexport) void __cdecl Rsxr_Mid_PSN2(RsxrMid::Context& c) {
    if (c.ebx >= 6) {
        Rsxr_PegNamed(RsxrMid::Ptr(c.esi), c.ebx);
    }
}
}
