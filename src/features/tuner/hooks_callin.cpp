// The calls written into the tuner copies: call-in entries (Rs7Mid::CallIn, docs/ARCHITECTURE.md section 15 "Call-in
// entries") that run these callbacks with the copy's registers. They replaced the naked Rs7_Call_PubEdge (the TU8A
// copy, tools/gen_tuner8.py) and the stdcall wrappers Rs7Tu8_Advance / Rs7Tu8_First (the session copies' synthetic
// calls, tools/gen_tuner8s.py), frozen at 304b9d3 (tools/tests/test_midhook_callin2.py). Full: per frame or per key
// press, C helpers.
#include "features/publish/publish.h"
#include "features/tuner/hooks.h"
#include "platform/midhook.h"

using Rs7Mid::Dword;
using Rs7Mid::Ptr;

extern "C" {
// TU8A 0x7E72D0, 13 bytes (`test cl,cl; jz; xor al,al; jmp; mov al,[eax+0Bh]; dec al`; EAX = InstrumentInfo, CL =
// first): EAX = the edge string (Rs7Publish_EdgeCopy). Other registers kept; flags kept (the old helper left the C
// helper's; dead at the copy).
__declspec(dllexport) void __cdecl Rs7_CallIn_PubEdge(Rs7Mid::Context& c) {
    c.eax = Rs7Publish_EdgeCopy(Ptr(c.eax), c.ecx & 0xFF);
}

// stdcall (session, dir) -> EAX, `ret 8`; ECX / EDX kept (the copies keep values there across the step).
__declspec(dllexport) void __cdecl Rs7_CallIn_Tu8Advance(Rs7Mid::Context& c) {
    c.eax = uint32_t(Rs7Tu8_AdvanceC(Ptr(Dword(c.esp + 4)), int(Dword(c.esp + 8))));
}

// stdcall (session) -> EAX, `ret 4`; ECX / EDX kept.
__declspec(dllexport) void __cdecl Rs7_CallIn_Tu8First(Rs7Mid::Context& c) {
    c.eax = Rs7Tu8_FirstC(Ptr(Dword(c.esp + 4)));
}
}

Rs7Mid::CallIn g_rs7_callin_PubEdge{&Rs7_CallIn_PubEdge, Rs7Mid::Mode::Full};
Rs7Mid::CallIn g_rs7_callin_Tu8Advance{&Rs7_CallIn_Tu8Advance, Rs7Mid::Mode::Full, 8};
Rs7Mid::CallIn g_rs7_callin_Tu8First{&Rs7_CallIn_Tu8First, Rs7Mid::Mode::Full, 4};
