// TF1 - 0x5B26B5 in the tuner camera pose 0x5B2680 (EAX = the guitar; not inverted): the stock reference string
// `mov edx,[eax+24h]; mov bl,[edx+0Bh]; dec bl` = count-1, the string at the edge opposite string 0. On a guitar built
// with 7/8 strings, strings 6/7 lie beyond string 0 (the extrapolated lanes), so that edge is still string 5:
// BL = min(count, 6) - 1. Bass (4) and six-string guitars: unchanged. Back to 0x5B26BD.
//
// Light mid-hook (per tuner frame, integer only; LIGHT_CALLBACKS in tools/tests/emu_harness.py), converted from a
// naked stub (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_cold.py). The old stub left the flags of
// its own `cmp bl,6; dec bl`; no continuation reads them (contract "free").
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Dword;

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_TF1 = 0;

namespace {
constexpr uint8_t kEdgeStrings = 6;
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TF1(Rs7Mid::Context& c) {
    c.edx = Dword(c.eax + 0x24);
    const uint8_t count = *reinterpret_cast<const uint8_t*>(c.edx + 0x0B);
    const uint8_t edge = count > kEdgeStrings ? kEdgeStrings : count;

    c.ebx = (c.ebx & 0xFFFFFF00u) | static_cast<uint8_t>(edge - 1);
    c.jump = static_cast<uint32_t>(g_rs7_resume_TF1);
}
