#pragma once
#include <cstdint>

namespace rs7pegcolour {
// Native, loaded PegSet only (caller verifies ownership). Keep copied named colours
// current after an external palette/mode change; animation scalars are untouched.
unsigned Refresh(uint8_t* pegset, const uint8_t* manager, uint32_t player);
}  // namespace rs7pegcolour
