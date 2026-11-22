// The native GuitarStrings' construction and profile mid-hooks (ARCHITECTURE.md section 14 "Wrap placement").
// Construction wrappers converted from naked stubs (frozen at b72bc52, tools/tests/test_midhook_asm_sites.py).
#include "features/strings/hooks.h"
#include "model/instrument.h"
#include "platform/safecopy.h"

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_target_GIP1 = 0;

// GIA1 - the call of the allocator 0x8F9D40 at 0x592627 in the guitar ctor (after `push 38h; mov [esi+4Ch],ebx`; cdecl
// (size, ...), the caller cleans): before, reserve size tracking, then request 48h (38h when full); after, finish
// registration or release the reservation on allocation failure, before the stock ctor. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rsxr_Mid_GIA1Before(RsxrMid::Context& c) {
    auto& frame = RsxrMid::Frame(c, kRsxrWrapGIA1);

    frame.user = rsxrgs::ReserveInfo();
    frame.Arg(0) = frame.user ? rsxrgs::kInfoSize : rsxrgs::kStockInfoSize;
}

__declspec(dllexport) void __cdecl Rsxr_Mid_GIA1(RsxrMid::Context& c) {
    rsxrgs::FinishInfo(RsxrMid::Frame(c, kRsxrWrapGIA1).user, RsxrMid::Ptr(c.eax));
}

// GSC1 - the call of Rnd_GuitarStrings_Ctor 0x599CE0 at 0x592651 after `push eax` (stdcall (the allocation), `ret 4`;
// ESI = the guitar). The entry runs the stock ctor itself, with the game's x87 state (a call from the full-mode
// callback would run it after the thunk's FNINIT); after it, RsxrGs_Construct adds the native layout. EAX = the
// GuitarStrings. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rsxr_Mid_GSC1(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    c.eax = uint32_t(uintptr_t(RsxrGs_Construct(RsxrMid::Ptr(c.esi), RsxrMid::Ptr(c.eax))));
}

// GIP1 0x4B240B: after Rnd_InstrumentInfo_FromProfile (0x58ABF3 writes the count), before the per-string
// initialization at 0x4B2416. EBX = holder, holder +4 = guitar. Full mode: once per profile assignment.
__declspec(dllexport) void __cdecl Rsxr_Mid_GIP1(RsxrMid::Context& c) {
    auto* guitar = Rsxr_SafeRead<uint8_t*>(RsxrMid::Ptr(c.ebx), 4);
    auto* gs = Rsxr_SafeRead<uint8_t*>(guitar, rsxr::kGuitarStrings);
    auto* info = Rsxr_SafeRead<uint8_t*>(guitar, rsxr::kGuitarInfo);

    rsxrgs::RefreshBuildCounts(gs, info);
}
}
