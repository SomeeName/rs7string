// V2 - 0x7A69BE in the slide offset 0x7A6720: the end cell read `movzx eax,al; imul eax,eax,1CCh; add eax,ecx;
// lea eax,[edx+eax+3Ch]` (EDX = string*4Ch, AL = fret-1, ECX = FretGrid; EAX = &cell + 3Ch, then three fld
// [eax+0/4/8]). The site is only the stride carrier: its 1CCh immediate is excluded from the in-place FretGrid edits
// (tools/gen_fretgrid.py EXCLUDED), so the hook multiplies by the row stride (1CCh stock, 264h with native_fretgrid).
// Strings 0-7 are real cells now ([neck] native_publish); there is no string-6 extrapolation.
//
// Light mid-hook (per slide gem, integer only; LIGHT_CALLBACKS in tools/tests/emu_harness.py), converted from a naked
// stub (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_cold.py). The old stub left the flags of its
// `add eax,ecx`; no continuation reads them (contract "free").
#include "features/fretgrid/fretgrid.h"
#include "platform/midhook.h"
#include <cstdint>

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_V2 = 0;

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_V2(Rs7Mid::Context& c) {
    c.eax = (c.eax & 0xFF) * rs7fg::RowStride() + c.ecx + c.edx + 0x3C;
    c.jump = static_cast<uint32_t>(g_rs7_resume_V2);
}
