#pragma once
#include <cstdint>

namespace rs7pegcolour {
// Native, loaded PegSet only (caller verifies ownership). Keep copied named colours
// current after an external palette/mode change; animation scalars are untouched.
unsigned Refresh(uint8_t* pegset, const uint8_t* manager, uint32_t player);
// The [mode * 3 + channel] palette an extra peg's named colour is derived from (rs7head::NamedColour): peg index 6
// (the 7th string) g_rs7_fx_rgb, index 7 (the 8th string) rs7colour::Colour8, written into buf.
const float* ExtraPalette(uint32_t index, float buf[6]);
}  // namespace rs7pegcolour
