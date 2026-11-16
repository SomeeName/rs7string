// The native GuitarStrings' construction and profile mid-hooks (ARCHITECTURE.md section 14 "Wrap placement").
// Construction wrappers converted from naked stubs (frozen at b72bc52, tools/tests/test_midhook_asm_sites.py).
#include "features/strings/hooks.h"
#include "model/instrument.h"
#include "platform/safecopy.h"

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_target_GIP1 = 0;

// GIA1 - the call of the allocator 0x8F9D40 at 0x592627 in the guitar ctor (after `push 38h; mov [esi+4Ch],ebx`; cdecl
// (size, ...), the caller cleans): before, reserve size tracking, then request 48h (38h when full); after, finish
// registration or release the reservation on allocation failure, before the stock ctor. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rs7_Mid_GIA1Before(Rs7Mid::Context& c) {
    auto& frame = Rs7Mid::Frame(c, kRs7WrapGIA1);

    frame.user = rs7gs::ReserveInfo();
    frame.Arg(0) = frame.user ? rs7gs::kInfoSize : rs7gs::kStockInfoSize;
}

__declspec(dllexport) void __cdecl Rs7_Mid_GIA1(Rs7Mid::Context& c) {
    rs7gs::FinishInfo(Rs7Mid::Frame(c, kRs7WrapGIA1).user, Rs7Mid::Ptr(c.eax));
}

// GSC1 - the call of Rnd_GuitarStrings_Ctor 0x599CE0 at 0x592651 after `push eax` (stdcall (the allocation), `ret 4`;
// ESI = the guitar). The entry runs the stock ctor itself, with the game's x87 state (a call from the full-mode
// callback would run it after the thunk's FNINIT); after it, Rs7Gs_Construct adds the native layout. EAX = the
// GuitarStrings. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rs7_Mid_GSC1(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    c.eax = uint32_t(uintptr_t(Rs7Gs_Construct(Rs7Mid::Ptr(c.esi), Rs7Mid::Ptr(c.eax))));
}

// GIP1 0x4B240B: after Rnd_InstrumentInfo_FromProfile (0x58ABF3 writes the count), before the per-string
// initialization at 0x4B2416. EBX = holder, holder +4 = guitar. Full mode: once per profile assignment.
__declspec(dllexport) void __cdecl Rs7_Mid_GIP1(Rs7Mid::Context& c) {
    auto* guitar = Rs7_SafeRead<uint8_t*>(Rs7Mid::Ptr(c.ebx), 4);
    auto* gs = Rs7_SafeRead<uint8_t*>(guitar, rs7::kGuitarStrings);
    auto* info = Rs7_SafeRead<uint8_t*>(guitar, rs7::kGuitarInfo);

    rs7gs::RefreshBuildCounts(gs, info);
}
}
