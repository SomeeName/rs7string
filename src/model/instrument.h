#pragma once
#include <cstdint>

// The game's instrument layout as the features read it (offsets only, no addresses): a play session's guitar
// (Rnd_Guitar), the guitar's objects and its InstrumentInfo. One copy here instead of one per feature.
namespace rs7 {
constexpr int kMaxGuitars = 8;              // per-guitar side tables: guitars alive at once (co-op, menus, tuner)
constexpr uint32_t kSessionGuitar = 0x37C;  // play session / player: Rnd_Guitar*
constexpr uint32_t kGuitarStrings = 0x0C;   // Rnd_Guitar: GuitarStrings*
constexpr uint32_t kGuitarGrid = 0x10;      // Rnd_Guitar: fret grid
constexpr uint32_t kGuitarHead = 0x18;      // Rnd_Guitar: Rnd_Headstock*
constexpr uint32_t kGuitarInfo = 0x24;      // Rnd_Guitar: InstrumentInfo*
constexpr uint32_t kInfoKind = 0x00;        // InstrumentInfo: u8 instrument kind (kKindBass = bass)
constexpr uint32_t kInfoLefty = 0x09;       // InstrumentInfo: u8 non-zero = left-handed
constexpr uint32_t kInfoStrings = 0x0B;     // InstrumentInfo: u8 string count the guitar was built with (PSL1)
constexpr uint32_t kInfoFrets = 0x0C;       // InstrumentInfo: u8 FretGrid row count (fret rows, <= 26)
constexpr uint32_t kInfoFretBars = 0x0D;    // InstrumentInfo: u8 fret bar count (25)
constexpr uint32_t kGridTail = 0x5C40;      // FretGrid: the tail (fretBar[26] x 38h first), same in stock and native
// The game's detector / InstrumentInfo instrument code for a bass (RS7X has its own, model/extblock.h).
constexpr int kKindBass = 2;

constexpr bool IsBassKind(int kind) {
    return kind == kKindBass;
}

// A Rnd_Guitar's InstrumentInfo (plain read, the caller guards faults), or null for a null guitar.
inline uint8_t* GuitarInfo(const void* guitar) {
    return guitar ? *reinterpret_cast<uint8_t* const*>(static_cast<const uint8_t*>(guitar) + kGuitarInfo) : nullptr;
}

// An extended instrument's built string count: guitar 7 / 8, bass 5 / 6.
constexpr bool IsExtendedGuitar(unsigned count) {
    return count == 7 || count == 8;
}

constexpr bool IsExtendedBass(unsigned count) {
    return count == 5 || count == 6;
}

// Extended bass: the stock string each bass string sits next to (stock strings: themselves; 4 low B next to the
// E 0, 5 high C next to the G 3).
constexpr uint8_t kBassNeighbour[6] = {0, 1, 2, 3, 0, 3};
}  // namespace rs7
