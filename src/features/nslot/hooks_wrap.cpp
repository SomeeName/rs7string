// Wrap-placement mid-hooks of the native N-slot chord code (ARCHITECTURE.md section 15 "Wrap placement"): the callee
// runs on copies of its arguments, the callbacks read the caller's own. Converted from naked stubs (frozen at b72bc52,
// tools/tests/test_midhook_wrap_sites.py).
#include "features/nslot/hooks.h"
#include "features/nslot/match.h"
#include "features/nslot/nslot.h"

extern "C" {
// PP1 - build the T8 / C8 arrays after SNG_Song_PostProcess. The call at 0x569137 (loader thread, full mode: the helper
// allocates and logs): after the stock call, Rs7Ns_OnPostProcessed(song = argument 0) re-syncs the T8 array and builds
// the C8 array. The caller reloads EAX at 0x56913C, so the callee's return value is kept unchanged.
__declspec(dllexport) void __cdecl Rs7_Mid_PP1(Rs7Mid::Context& c) {
    Rs7Ns_OnPostProcessed(Rs7Mid::Ptr<void>(Rs7Mid::Frame(c, kRs7WrapPP1).Arg(0)));
}

// AM1 - chord-mistake analysis with eight shifted targets. 0x87567D in Play_AnalyzeChordMistake 0x8755C0: the call of
// 0x4E6A70 (stdcall targets, tolerance, two time floats; ESI = matcher) with the chord's six targets shifted by EDI
// semitones (argument 0 = EBP-1Ch; [EBP-44h] = the template, a T8). The callee gets an int32[8] in the scratch instead:
// those six plus the T8's strings 6/7 shifted the same way (Rs7Ns_Shift8, integer only). No after callback. Full mode:
// once per analysed mistake.
__declspec(dllexport) void __cdecl Rs7_Mid_AM1Before(Rs7Mid::Context& c) {
    Rs7Mid::WrapFrame& f = Rs7Mid::Frame(c, kRs7WrapAM1);
    int32_t* eight = reinterpret_cast<int32_t*>(Rs7Mid::Locals(c));

    Rs7Ns_Shift8(eight, Rs7Mid::Ptr<const int32_t>(f.Arg(0)), Rs7Mid::Ptr<const uint8_t>(Rs7Mid::Dword(c.ebp - 0x44)),
                 int(c.edi));
    f.Arg(0) = uint32_t(uintptr_t(eight));
}

// MD1 - chord detection over eight targets: the entry of Play_Matcher_DetectChord 0x4E7B30 (stdcall: matcher, int32
// targets[8], tolerance, history index; AL = matched; `ret 10h`). Rs7Ns_DetectChord compacts up to six targets into the
// scratch's int32[6] (returns -1: the stock function runs on it, its prologue replayed) or judges seven / eight targets
// itself (returns 0/1, the stock function skipped). Full mode: per judged chord, a few calls per frame at most.
__declspec(dllexport) void __cdecl Rs7_Mid_MD1Before(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Mid::WrapFrame& f = Rs7Mid::Frame(c, kRs7WrapMD1);
    int32_t* six = reinterpret_cast<int32_t*>(Rs7Mid::Locals(c));
    const int judged = Rs7Ns_DetectChord(Rs7Mid::Ptr<const uint8_t>(f.Arg(0)), Rs7Mid::Ptr<const int32_t>(f.Arg(1)),
                                         int(f.Arg(2)), int(f.Arg(3)), six);

    if (judged != -1) {
        Rs7Mid::WrapReturn(c, kRs7WrapMD1, uint32_t(judged));
        return;
    }

    f.Arg(1) = uint32_t(uintptr_t(six));
}
}
