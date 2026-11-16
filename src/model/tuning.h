#pragma once
#include <cstdint>

// The tuning of the extended strings, shared by the pitch, publication and colour code.
namespace rs7 {
constexpr int kLowB = 35;  // B1: base MIDI of string index 6
// Open-string base MIDI per string index. Guitar: E2 A2 D3 G3 B3 E4, then B1 and F#1 (7th / 8th string).
// Bass: E1 A1 D2 G2, then the extended low B0 (string 4) and high C3 (string 5).
constexpr int kGuitarOpenMidi[8] = {40, 45, 50, 55, 59, 64, kLowB, 30};
constexpr int kBassOpenMidi[6] = {28, 33, 38, 43, 23, 48};
constexpr int kNotActive = INT32_MIN;  // Rs7_Base6: not in 7-string mode
}  // namespace rs7

// The game's std::vector<int16> (MSVC 2010): begin at +0, end at +4.
struct Rs7Int16Vec {
    const int16_t* begin;
    const int16_t* end;
};
