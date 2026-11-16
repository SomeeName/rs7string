// model/instrument.h and model/slots.h: the instrument predicates and string counts every feature shares.
#include "model/instrument.h"
#include "model/slots.h"
#include "model/tuning.h"
#include "test.h"
#include <cstring>

// Catches: a bass check that also matches guitar (0/1) or another instrument code.
TEST(model_is_bass_kind_matches_only_the_bass_code) {
    for (int kind : {-1, 0, 1, 3, 255}) {
        CHECK(!rs7::IsBassKind(kind));
    }

    CHECK(rs7::IsBassKind(rs7::kKindBass));
}

// Catches: a stock (6-string guitar / 4-string bass) or an unsupported count taken for an extended instrument.
TEST(model_extended_string_counts) {
    struct Row {
        unsigned count;
        bool guitar, bass;
    };

    const Row rows[] = {{0, false, false}, {4, false, false}, {5, false, true},  {6, false, true},
                        {7, true, false},  {8, true, false},  {9, false, false}, {0xFFFFFFFFu, false, false}};

    for (const Row& r : rows) {
        CHECK_EQ(rs7::IsExtendedGuitar(r.count), r.guitar);
        CHECK_EQ(rs7::IsExtendedBass(r.count), r.bass);
    }
}

// Catches: GuitarInfo reading the wrong offset, or dereferencing a null guitar.
TEST(model_guitar_info_reads_the_info_pointer) {
    uint8_t info[0x10] = {};
    uint8_t guitar[0x40] = {};
    uint8_t* p = info;

    std::memcpy(guitar + rs7::kGuitarInfo, &p, sizeof p);
    CHECK(rs7::GuitarInfo(guitar) == info);
    CHECK(rs7::GuitarInfo(nullptr) == nullptr);
    std::memset(guitar + rs7::kGuitarInfo, 0, sizeof p);
    CHECK(rs7::GuitarInfo(guitar) == nullptr);
}

// Catches: the bass neighbour table pointing an extended string at a missing stock string (>= 4) or a stock string
// away from itself; the low B must sit next to the E, the high C next to the G.
TEST(model_bass_neighbour_table) {
    for (unsigned s = 0; s < rs7::kStockBassStrings; ++s) {
        CHECK_EQ(static_cast<unsigned>(rs7::kBassNeighbour[s]), s);
    }

    CHECK_EQ(static_cast<unsigned>(rs7::kBassNeighbour[4]), 0u);
    CHECK_EQ(static_cast<unsigned>(rs7::kBassNeighbour[5]), 3u);
    CHECK_EQ(sizeof rs7::kBassNeighbour, size_t{rs7::kMaxBassSlots});
}

// Catches: swapped bass / guitar limits, and GuitarStrings accepting a corrupt tuning size (0, 9, a bass count).
TEST(model_slot_limits_and_guitar_strings) {
    CHECK_EQ(rs7::MaxSlots(false), 8u);
    CHECK_EQ(rs7::MaxSlots(true), 6u);
    CHECK_EQ(rs7::StockStrings(false), 6u);
    CHECK_EQ(rs7::StockStrings(true), 4u);

    const size_t entries[] = {0, 4, 5, 6, 7, 8, 9, 100};
    const unsigned want[] = {6, 6, 6, 6, 7, 8, 6, 6};

    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; ++i) {
        CHECK_EQ(rs7::GuitarStrings(entries[i]), want[i]);
    }
}

// Catches: an open-MIDI table edit that breaks the extended strings (guitar low B / F#, bass low B / high C), whose
// pitches are a fourth below / above their neighbours.
TEST(model_open_midi_of_the_extended_strings) {
    CHECK_EQ(rs7::kGuitarOpenMidi[6], rs7::kGuitarOpenMidi[0] - 5);
    CHECK_EQ(rs7::kGuitarOpenMidi[7], rs7::kGuitarOpenMidi[6] - 5);
    CHECK_EQ(rs7::kBassOpenMidi[4], rs7::kBassOpenMidi[0] - 5);
    CHECK_EQ(rs7::kBassOpenMidi[5], rs7::kBassOpenMidi[3] + 5);
}
