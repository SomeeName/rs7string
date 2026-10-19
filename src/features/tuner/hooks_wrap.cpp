// The tuning names' Wrap mid-hooks (ARCHITECTURE.md sections 8 and 15 "Wrap placement"): TJ, TU and TN enter the three
// tuning-name resolvers, run the stock resolver on copies of its arguments and name a 7-string tuning before and after
// it; the handle of the open call goes from the before to the after callback in WrapFrame::user. Full mode: measured in
// game, TJ about 194 calls per song-list build, TU one per song start, TN one every 2-3 s in a tuner, none per frame
// (ARCHITECTURE.md section 15). Converted from naked stubs (frozen at b72bc52, tools/tests/test_midhook_wrap_sites.py).
#include "features/tuner/hooks.h"

using Rs7Mid::Context;
using Rs7Mid::Frame;

extern "C" {
// TJ - entry of the manifest tuning-name resolver 0x56C950 (ECX = raw out, EDX = display out, [esp+4] = owned Tuning
// JSON, [esp+8] = CentOffset; cdecl, void). Before: Rs7_TnJsonPre reads a 7-string Tuning (the original releases it);
// after the original: Rs7_TnJsonPost assigns the outputs.
__declspec(dllexport) void __cdecl Rs7_Mid_TJBefore(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (g_rs7_tn_on) {
        Rs7Mid::WrapFrame& f = Frame(c, kRs7WrapTJ);

        f.user = Rs7_TnJsonPre(Rs7Mid::Ptr<void>(c.ecx), Rs7Mid::Ptr<void>(c.edx), Rs7Mid::Ptr<void>(f.Arg(0)),
                               int(f.Arg(1)));
    }
}

__declspec(dllexport) void __cdecl Rs7_Mid_TJ(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (const uint32_t handle = Frame(c, kRs7WrapTJ).user) {
        Rs7_TnJsonPost(handle);
    }
}

// TU - entry of the arrangement-UUID resolver 0x56C300 (ECX = UUID*, EDX = output; EAX = output; no stack arguments).
// The original runs between Rs7_TnUuidPre and Rs7_TnUuidPost; TUT tells the open call which Tuning it found.
__declspec(dllexport) void __cdecl Rs7_Mid_TUBefore(Context& c) {
    if (g_rs7_tn_on) {
        Frame(c, kRs7WrapTU).user = Rs7_TnUuidPre(Rs7Mid::Ptr<void>(c.edx));
    }
}

__declspec(dllexport) void __cdecl Rs7_Mid_TU(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (const uint32_t handle = Frame(c, kRs7WrapTU).user) {
        Rs7_TnUuidPost(handle);
    }
}

// TN - entry of the tuner's array resolver 0x5B7750 (cdecl: [esp+4] = output string, [esp+8] = int[6]; EAX = output).
// Rs7_TnArrayPre constructs a known 7-string name itself (the original is skipped, EAX = the output); an unknown
// 7-string tuning runs the original and Rs7_TnArrayPost marks its result.
__declspec(dllexport) void __cdecl Rs7_Mid_TNBefore(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Mid::WrapFrame& f = Frame(c, kRs7WrapTN);

    if (g_rs7_tn_on && Rs7_TnArrayPre(Rs7Mid::Ptr<void>(f.Arg(0)), Rs7Mid::Ptr<const int>(f.Arg(1)), &f.user)) {
        Rs7Mid::WrapReturn(c, kRs7WrapTN, f.Arg(0));
    }
}

__declspec(dllexport) void __cdecl Rs7_Mid_TN(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Mid::WrapFrame& f = Frame(c, kRs7WrapTN);

    if (f.user) {
        Rs7_TnArrayPost(Rs7Mid::Ptr<void>(f.Arg(0)), f.user);
    }
}
}
