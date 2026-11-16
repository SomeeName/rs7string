#pragma once
#include <cstddef>

#include "model/instrument.h"

// String counts and slot ranges of the instruments. Stock strings keep their indices; the extra strings follow them
// (guitar 6 / 7 = low B / F#, bass 4 / 5 = low B / high C, model/tuning.h). Limits that differ on purpose (a reader
// that only knows some strings) name their own constant instead of these.
namespace rs7 {
constexpr unsigned kStockGuitarStrings = 6;
constexpr unsigned kStockBassStrings = 4;
constexpr unsigned kMaxGuitarSlots = 8;
constexpr unsigned kMaxBassSlots = 6;

constexpr unsigned MaxSlots(bool bass) {
    return bass ? kMaxBassSlots : kMaxGuitarSlots;
}

constexpr unsigned StockStrings(bool bass) {
    return bass ? kStockBassStrings : kStockGuitarStrings;
}

// A guitar arrangement's string count from its SNG tuning entries: 7 or 8 as given, anything else plays stock.
constexpr unsigned GuitarStrings(size_t entries) {
    return IsExtendedGuitar(unsigned(entries)) ? unsigned(entries) : kStockGuitarStrings;
}
}  // namespace rs7
