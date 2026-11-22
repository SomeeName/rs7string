// The tuning names' Wrap mid-hooks (ARCHITECTURE.md sections 8 and 15 "Wrap placement"): TJ, TU and TN enter the three
// tuning-name resolvers, run the stock resolver on copies of its arguments and name a 7-string tuning before and after
// it; the handle of the open call goes from the before to the after callback in WrapFrame::user. Full mode: measured in
// game, TJ about 194 calls per song-list build, TU one per song start, TN one every 2-3 s in a tuner, none per frame
// (ARCHITECTURE.md section 14). Converted from naked stubs (frozen at b72bc52, tools/tests/test_midhook_wrap_sites.py).
#include "features/tuner/hooks.h"

using RsxrMid::Context;
using RsxrMid::Frame;

extern "C" {
// TJ - entry of the manifest tuning-name resolver 0x56C950 (ECX = raw out, EDX = display out, [esp+4] = owned Tuning
// JSON, [esp+8] = CentOffset; cdecl, void). Before: Rsxr_TnJsonPre reads a 7-string Tuning (the original releases it);
// after the original: Rsxr_TnJsonPost assigns the outputs.
__declspec(dllexport) void __cdecl Rsxr_Mid_TJBefore(Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (g_rsxr_tn_on) {
        RsxrMid::WrapFrame& f = Frame(c, kRsxrWrapTJ);

        f.user = Rsxr_TnJsonPre(RsxrMid::Ptr<void>(c.ecx), RsxrMid::Ptr<void>(c.edx), RsxrMid::Ptr<void>(f.Arg(0)),
                                int(f.Arg(1)));
    }
}

__declspec(dllexport) void __cdecl Rsxr_Mid_TJ(Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (const uint32_t handle = Frame(c, kRsxrWrapTJ).user) {
        Rsxr_TnJsonPost(handle);
    }
}

// TU - entry of the arrangement-UUID resolver 0x56C300 (ECX = UUID*, EDX = output; EAX = output; no stack arguments).
// The original runs between Rsxr_TnUuidPre and Rsxr_TnUuidPost; TUT tells the open call which Tuning it found.
__declspec(dllexport) void __cdecl Rsxr_Mid_TUBefore(Context& c) {
    if (g_rsxr_tn_on) {
        Frame(c, kRsxrWrapTU).user = Rsxr_TnUuidPre(RsxrMid::Ptr<void>(c.edx));
    }
}

__declspec(dllexport) void __cdecl Rsxr_Mid_TU(Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (const uint32_t handle = Frame(c, kRsxrWrapTU).user) {
        Rsxr_TnUuidPost(handle);
    }
}

// TN - entry of the tuner's array resolver 0x5B7750 (cdecl: [esp+4] = output string, [esp+8] = int[6]; EAX = output).
// Rsxr_TnArrayPre constructs a known 7-string name itself (the original is skipped, EAX = the output); an unknown
// 7-string tuning runs the original and Rsxr_TnArrayPost marks its result.
__declspec(dllexport) void __cdecl Rsxr_Mid_TNBefore(Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    RsxrMid::WrapFrame& f = Frame(c, kRsxrWrapTN);

    if (g_rsxr_tn_on && Rsxr_TnArrayPre(RsxrMid::Ptr<void>(f.Arg(0)), RsxrMid::Ptr<const int>(f.Arg(1)), &f.user)) {
        RsxrMid::WrapReturn(c, kRsxrWrapTN, f.Arg(0));
    }
}

__declspec(dllexport) void __cdecl Rsxr_Mid_TN(Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    RsxrMid::WrapFrame& f = Frame(c, kRsxrWrapTN);

    if (f.user) {
        Rsxr_TnArrayPost(RsxrMid::Ptr<void>(f.Arg(0)), f.user);
    }
}
}
