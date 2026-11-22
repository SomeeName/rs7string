// Call-site mid-hooks of the native N-slot chord code (Before placement, ARCHITECTURE.md section 14 "Call sites"): the
// callback runs at the callee's entry with the game's return address at [esp] and the arguments above it. Light mode
// (integer only, LIGHT_CALLBACKS in tools/tests/emu_harness.py): both run per chord at play time. Each callback serves
// several sites that differ only in the callee, which the entry binds. Converted from naked stubs (frozen at b6071a8,
// tools/tests/test_midhook_calls.py); the old stubs' flags are dead at every exit.
#include "features/nslot/nslot.h"
#include "platform/midhook.h"
#include <cstdint>

using RsxrMid::Context;

extern "C" {
// The callees (bound by Rsxr_Startup; the entries call them directly).
__declspec(dllexport) uintptr_t g_rsxr_target_TM1 = 0, g_rsxr_target_TM2 = 0, g_rsxr_target_TM3 = 0;
__declspec(dllexport) uintptr_t g_rsxr_target_TM4 = 0, g_rsxr_target_TM5 = 0, g_rsxr_target_TM6 = 0;
__declspec(dllexport) uintptr_t g_rsxr_target_EV1 = 0, g_rsxr_target_EV2 = 0, g_rsxr_target_EV3 = 0;
__declspec(dllexport) uintptr_t g_rsxr_target_EV4 = 0, g_rsxr_target_EV5 = 0;

// TM1-TM6 - string-mask test over the eight-slot C8 mask. The calls of SNG_ChordNotes_TestStringMask 0x5581C0 in the
// neck-target code (0x7A56F0, 0x7A5D50, 0x7A6A50): ECX = ChordNotes record (a C8 at play time, the callers test it for
// NULL), AL = string, [esp+4] = mask flag, `ret 4`. The callee is replaced: AL = (string < 8 && C8 mask[8][string] &
// flag), as stock computes it over mask[6]. A string past the mask clears AL only; otherwise EAX = 0/1 and EDX = the
// masked word (stock clobbers EDX too).
__declspec(dllexport) void __cdecl Rsxr_Mid_TM(Context& c) {
    uint32_t bits = 0;

    if (!rsxrns::StringMask8(reinterpret_cast<const uint8_t*>(c.ecx), c.eax & 0xFF, RsxrMid::Dword(c.esp + 4), &bits)) {
        RsxrMid::Return(c, c.eax & ~0xFFu, 4);
        return;
    }

    c.edx = bits;
    RsxrMid::Return(c, bits != 0 ? 1u : 0u, 4);
}

// EV1-EV5 - eval target arrays: eval+2Ch (six) becomes eval+284h (eight). The calls of the chord matcher wrappers with
// an eval's six targets eval+2Ch as the first stack argument (0x876A40 / 0x87762F -> 0x4E6630, 0x877680 -> 0x4E6680,
// 0x876A8A / 0x877730 -> 0x4E6A70): the argument becomes eval+284h, the eval's int32[8] (2Ch + 258h), then the
// callee runs with the game's stack and return address.
__declspec(dllexport) void __cdecl Rsxr_Mid_EV(Context& c) {
    *reinterpret_cast<uint32_t*>(c.esp + 4) += rsxrns::kEvMidi8 - rsxrns::kEvMidi6;
}
}

static_assert(rsxrns::kEvMidi8 - rsxrns::kEvMidi6 == 0x258, "EV1-EV5: eval+2Ch -> eval+284h");
