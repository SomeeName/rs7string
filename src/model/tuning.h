#pragma once
#include <cstdint>

// The tuning of the extended strings, shared by the pitch, publication and colour code.
namespace rs7 {
constexpr int kLowB = 35;              // B1: base MIDI of string index 6
constexpr int kNotActive = INT32_MIN;  // Rs7_Base6: not in 7-string mode
}  // namespace rs7

// The game's std::vector<int16> (MSVC 2010): begin at +0, end at +4.
struct Rs7Int16Vec {
    const int16_t* begin;
    const int16_t* end;
};
