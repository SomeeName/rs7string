// An extended bass's low B gets colour slot 6 per player (#170): the three leaf getters that turn a string index into
// a colour (CONFIRMED, docs/research bass-lowb-slot.md) are hooked at their entry and the string argument is remapped
// for a player whose bass was built with 5 / 6 strings (SetBassShift). The CM ports already let slots 6 / 7 through
// their `cmp edx,6` guards. A guitar player's B string keeps slot 4, so co-op no longer shares it.
// HYPOTHESIS: the colour manager's player index is C-lite's load player (both are local co-op player 0 / 1).
// Light mode: integer only, no calls.
#include "features/colours/colours.h"
#include <windows.h>

namespace {
volatile LONG g_shift[2] = {};

bool Shifted(uint32_t player) {
    return player < 2 && g_shift[player] != 0;
}

// A string index in the low byte of a register (CL): the remapped byte, the rest kept.
uint32_t RemapLowByte(uint32_t reg, uint32_t player) {
    return (reg & ~0xFFu) | rsxrcolour::BassSlot(reg & 0xFFu, Shifted(player));
}
}  // namespace

void rsxrcolour::SetBassShift(unsigned player, bool on) {
    if (player < 2) {
        InterlockedExchange(&g_shift[player], on ? 1 : 0);
    }
}

// CBUV 0x7A8590 Rnd_Gem_GetColorUV entry: EAX = player, CL = string (notes, sustains, neck targets, chord notes).
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_CBUV(RsxrMid::Context& c) {
    c.ecx = RemapLowByte(c.ecx, c.eax);
}

// CBRGB 0x7A8490 Rnd_Gem_GetColorRGB entry: ECX = player, the string the byte at [esp+8] (the second argument).
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_CBRGB(RsxrMid::Context& c) {
    uint8_t* string = reinterpret_cast<uint8_t*>(c.esp + 8);

    *string = uint8_t(rsxrcolour::BassSlot(*string, Shifted(c.ecx)));
}

// CBSTR 0x598120 Rnd_GuitarString_GetStateColor entry: EAX = player, CL = string (the string lines).
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_CBSTR(RsxrMid::Context& c) {
    c.ecx = RemapLowByte(c.ecx, c.eax);
}
