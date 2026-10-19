// The native GuitarStrings' Wrap mid-hooks (ARCHITECTURE.md section 15 "Wrap placement"). Converted from naked stubs
// (frozen at b72bc52, tools/tests/test_midhook_asm_sites.py).
#include "features/strings/hooks.h"

extern "C" {
// GIA1 - the call of the allocator 0x8F9D40 at 0x592627 in the guitar ctor (after `push 38h; mov [esi+4Ch],ebx`; cdecl
// (size, ...), the caller cleans): before, the size argument becomes the native 48h; after, a non-NULL InstrumentInfo
// gets its extension (Rs7Gs_InfoAllocated) before the stock ctor runs. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rs7_Mid_GIA1Before(Rs7Mid::Context& c) {
    Rs7Mid::Frame(c, kRs7WrapGIA1).Arg(0) = rs7gs::kInfoSize;
}

__declspec(dllexport) void __cdecl Rs7_Mid_GIA1(Rs7Mid::Context& c) {
    if (c.eax) {
        Rs7Gs_InfoAllocated(Rs7Mid::Ptr(c.eax));
    }
}

// GSC1 - the call of Rnd_GuitarStrings_Ctor 0x599CE0 at 0x592651 after `push eax` (stdcall (the allocation), `ret 4`;
// ESI = the guitar). The entry runs the stock ctor itself, with the game's x87 state (a call from the full-mode
// callback would run it after the thunk's FNINIT); after it, Rs7Gs_Construct adds the native layout. EAX = the
// GuitarStrings. Full mode: once per guitar.
__declspec(dllexport) void __cdecl Rs7_Mid_GSC1(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    c.eax = uint32_t(uintptr_t(Rs7Gs_Construct(Rs7Mid::Ptr(c.esi), Rs7Mid::Ptr(c.eax))));
}
}
