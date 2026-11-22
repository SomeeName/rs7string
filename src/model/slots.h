#pragma once
#include <cstddef>

#include "model/instrument.h"

// String counts and slot ranges of the instruments. Stock strings keep their indices; the extra strings follow them
// (guitar 6 / 7 = low B / F#, bass 4 / 5 = low B / high C, model/tuning.h). Limits that differ on purpose (a reader
// that only knows some strings) name their own constant instead of these.
namespace rsxr {
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

// The game slot of string i of a native chart, counted from the thickest (docs/NATIVE-FORMAT.md, "String
// numbering"); -1 for a string count the instrument does not have.
constexpr int NativeSlot(bool bass, int count, int i) {
    if (bass) {
        if (count < 4 || count > 6) {
            return -1;
        }

        if (count == 4) {
            return i;
        }

        return i == 0 ? 4 : i == 5 ? 5 : i - 1;
    }

    if (count < 6 || count > 8) {
        return -1;
    }

    const int extra = count - 6;  // strings below the low E

    return i < extra ? 6 + (extra - 1 - i) : i - extra;
}
}  // namespace rsxr
