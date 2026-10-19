// BD1-BD4: the four `cmp <string>,6; jae` guards in front of a perStringMode read (info +20h + string*4: 1 for
// strings 0-2, 0 for 3-5; it picks the bend direction). Stock treats string >= 6 as mode 2 (no offset / the other
// branch), so a string-6 bend went the wrong way (issue #14). String 6 now reads perStringMode[0], string 0's
// direction (it sits next to string 0). Every other string runs the replayed `cmp; jae` (strings 0-5 -> the stock
// body at resume, > 6 -> the stock target). The string-6 path does what the stock body does with index 0 and joins
// the stock code after it.
//
// Light mid-hooks (per gem on the note highway, integer only; LIGHT_CALLBACKS in tools/tests/emu_harness.py). The
// old stubs left the flags of their own `add [esp],n` at the join; no continuation reads them
// (tools/tests/test_midhook_cold.py, contract "free").
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Dword;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_BD1 = 0, g_rs7_target_BD1 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_BD2 = 0, g_rs7_target_BD2 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_BD3 = 0, g_rs7_target_BD3 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_BD4 = 0, g_rs7_target_BD4 = 0;
}

namespace {
constexpr uint32_t kString6 = 6;

// String 0's perStringMode of an InstrumentInfo.
uint32_t Mode0(uint32_t info) {
    return Dword(info + 0x20);
}
}  // namespace

// 0x79FE9B, CL = string, EAX = info. Body `movzx edx,cl; cmp dword [eax+edx*4+20h],0; jne target`; mode 0
// continues at resume + 0Ah (after the body's jne).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_BD1(Rs7Mid::Context& c) {
    if (uint8_t(c.ecx) != kString6) {
        return;
    }

    c.edx = kString6;
    c.jump = Mode0(c.eax) == 0 ? uint32_t(g_rs7_resume_BD1) + 0x0A : uint32_t(g_rs7_target_BD1);
}

// 0x7A663B, CL = string, EAX = info. Body `movzx edx,cl; mov ecx,[eax+edx*4+20h]; jmp`, joins at resume + 0Eh.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_BD2(Rs7Mid::Context& c) {
    if (uint8_t(c.ecx) != kString6) {
        return;
    }

    c.edx = kString6;
    c.ecx = Mode0(c.eax);
    c.jump = uint32_t(g_rs7_resume_BD2) + 0x0E;
}

// 0x7A6D80, BL = string, EDI = session. Body `mov ecx,[edi+37Ch]; mov edx,[ecx+24h]; movzx eax,bl;
// mov edi,[edx+eax*4+20h]; jmp`, joins at resume + 17h.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_BD3(Rs7Mid::Context& c) {
    if (uint8_t(c.ebx) != kString6) {
        return;
    }

    c.ecx = Dword(c.edi + 0x37C);
    c.edx = Dword(c.ecx + 0x24);
    c.eax = kString6;
    c.edi = Mode0(c.edx);
    c.jump = uint32_t(g_rs7_resume_BD3) + 0x17;
}

// 0x7A764D, BL = string, EAX = guitar. Body `mov edx,[eax+24h]; movzx ecx,bl; mov eax,[edx+ecx*4+20h];
// mov [ebp-18h],eax; jmp`, joins at resume + 20h.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_BD4(Rs7Mid::Context& c) {
    if (uint8_t(c.ebx) != kString6) {
        return;
    }

    c.edx = Dword(c.eax + 0x24);
    c.ecx = kString6;
    c.eax = Mode0(c.edx);
    *reinterpret_cast<uint32_t*>(c.ebp - 0x18) = c.eax;
    c.jump = uint32_t(g_rs7_resume_BD4) + 0x20;
}
