// The fret bars' Wrap mid-hooks (ARCHITECTURE.md section 15 "Wrap placement"): F1 runs the stock
// Rnd_FretBars_ApplyTransforms and then Rs7_FretsAfterApply, IN1 the stock inlay placement and then Rs7_InlaysAfter.
// Full mode (both helpers use floats); a few calls per guitar transform update. Converted from naked stubs (frozen at
// b72bc52, tools/tests/test_midhook_wrap_sites.py). Group Core (registration.cpp).
#include "features/frets/hooks.h"

using Rs7Mid::Context;

extern "C" {
// F1 - entry of Rnd_FretBars_ApplyTransforms 0x58FB20 (EAX = FretGrid, ECX = guitar; no stack arguments, plain `ret`,
// x87 empty at both ends). The stock function runs (prologue `push ebp; mov ebp,esp; sub esp,34h` replayed); after its
// `ret`, Rs7_FretsAfterApply(grid = EAX at the call) re-places the bars of a guitar that shows the 7th string. The
// registers the stock function returns with are kept; flags are dead after a call.
__declspec(dllexport) void __cdecl Rs7_Mid_F1(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7_FretsAfterApply(Rs7Mid::Ptr(Rs7Mid::Frame(c, kRs7WrapF1).eax));
}

// IN1 - the call of Rnd_Inlays_ApplyTransforms 0x59BA80 at 0x593074 (EAX = guitar, [esp+4] = the inlay set, `ret 4`):
// the stock call, then Rs7_InlaysAfter(guitar, set) re-places the inlay entity for a guitar built with 7/8 strings.
// EAX comes back as the guitar, as the old stub left it (the caller reloads EAX at 0x593079).
__declspec(dllexport) void __cdecl Rs7_Mid_IN1(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Mid::WrapFrame& f = Rs7Mid::Frame(c, kRs7WrapIN1);

    Rs7_InlaysAfter(Rs7Mid::Ptr(f.eax), Rs7Mid::Ptr(f.Arg(0)));
    c.eax = f.eax;
}
}
