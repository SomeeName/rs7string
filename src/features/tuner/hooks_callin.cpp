// The calls written into the tuner copies: call-in entries (RsxrMid::CallIn, docs/ARCHITECTURE.md section 14 "Call-in
// entries") that run these callbacks with the copy's registers. They replaced the naked Rsxr_Call_PubEdge (the TU8A
// copy, tools/gen_tuner8.py) and the stdcall wrappers RsxrTu8_Advance / RsxrTu8_First (the session copies' synthetic
// calls, tools/gen_tuner8s.py), frozen at 304b9d3 (tools/tests/test_midhook_callin2.py). Full: per frame or per key
// press, C helpers.
#include "features/publish/publish.h"
#include "features/tuner/hooks.h"
#include "platform/midhook.h"

using RsxrMid::Dword;
using RsxrMid::Ptr;

extern "C" {
// TU8A 0x7E72D0, 13 bytes (`test cl,cl; jz; xor al,al; jmp; mov al,[eax+0Bh]; dec al`; EAX = InstrumentInfo, CL =
// first): EAX = the edge string (RsxrPublish_EdgeCopy). Other registers kept; flags kept (the old helper left the C
// helper's; dead at the copy).
__declspec(dllexport) void __cdecl Rsxr_CallIn_PubEdge(RsxrMid::Context& c) {
    c.eax = RsxrPublish_EdgeCopy(Ptr(c.eax), c.ecx & 0xFF);
}

// stdcall (session, dir) -> EAX, `ret 8`; ECX / EDX kept (the copies keep values there across the step).
__declspec(dllexport) void __cdecl Rsxr_CallIn_Tu8Advance(RsxrMid::Context& c) {
    c.eax = uint32_t(RsxrTu8_AdvanceC(Ptr(Dword(c.esp + 4)), int(Dword(c.esp + 8))));
}

// stdcall (session) -> EAX, `ret 4`; ECX / EDX kept.
__declspec(dllexport) void __cdecl Rsxr_CallIn_Tu8First(RsxrMid::Context& c) {
    c.eax = RsxrTu8_FirstC(Ptr(Dword(c.esp + 4)));
}
}

RsxrMid::CallIn g_rsxr_callin_PubEdge{&Rsxr_CallIn_PubEdge, RsxrMid::Mode::Full};
RsxrMid::CallIn g_rsxr_callin_Tu8Advance{&Rsxr_CallIn_Tu8Advance, RsxrMid::Mode::Full, 8};
RsxrMid::CallIn g_rsxr_callin_Tu8First{&Rsxr_CallIn_Tu8First, RsxrMid::Mode::Full, 4};
