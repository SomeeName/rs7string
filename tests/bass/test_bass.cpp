#include "features/bass/bass.h"
#include "features/fretgrid/fretgrid.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "model/extblock.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cstring>
#include <memory>
#include <vector>

using namespace rsxrbass;

TEST(bass_song_strings_need_a_bass_block_on_a_bass_detector) {
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentBass, 5, 2), 5u);
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentBass, 6, 2), 6u);
    CHECK_EQ(SongStrings(false, rsxrext::kInstrumentBass, 5, 2), 0u);  // no extension block: stock 4-string bass
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentGuitar, 7, 2), 0u);
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentBass, 5, 1), 0u);  // a guitar detector
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentBass, 4, 2), 0u);
    CHECK_EQ(SongStrings(true, rsxrext::kInstrumentBass, 7, 2), 0u);
}

TEST(bass_extra_bases_and_lowest_open) {
    CHECK_EQ(ExtraBase(4, 5), kBaseB);
    CHECK_EQ(ExtraBase(5, 5), kNoBase);  // five strings: slot 5 is padding
    CHECK_EQ(ExtraBase(5, 6), kBaseC);
    CHECK_EQ(ExtraBase(4, 0), kNoBase);
    CHECK_EQ(ExtraBase(3, 6), kNoBase);
    CHECK_EQ(ExtraBase(6, 6), kNoBase);
    CHECK_EQ(ExtraBase(4, 7), kNoBase);
    const int16_t standard[6] = {0, 0, 0, 0, 0, 0}, dropA[6] = {-2, 0, 0, 0, -2, 0};

    CHECK_EQ(Lowest(28, standard, 5), 23);  // B0
    CHECK_EQ(Lowest(26, dropA, 6), 21);     // A0
    CHECK_EQ(Lowest(28, standard, 0), 28);
    const int16_t high[6] = {0, 0, 0, 0, 0, -30};

    CHECK_EQ(Lowest(28, high, 6), 18);  // C tuned below B: the lowest of both
}

TEST(bass_attach_extrapolates_outwards) {
    const uint8_t bass[0x0C] = {2}, guitar[0x0C] = {1};
    uint32_t nearRow = 9, farRow = 9;

    CHECK_EQ(rsxrgs::StockAttachCount(bass), 4u);
    CHECK_EQ(rsxrgs::StockAttachCount(guitar), 6u);
    rsxrgs::BassAttachRows(4, &nearRow, &farRow);  // low B below E
    CHECK(nearRow == 0 && farRow == 1);
    rsxrgs::BassAttachRows(5, &nearRow, &farRow);  // high C above G
    CHECK(nearRow == 3 && farRow == 2);
}

TEST(bass_added_strings_draw_the_bass_rows_of_their_neighbours) {
    CHECK(rsxrgs::BassStringRow(4) == 0.0f);  // low B: the E's row (stock 0.4 read past the bass table)
    CHECK(rsxrgs::BassStringRow(5) == 0.3f);  // high C: the G's row
}

namespace {
// A fake song (tuning vector at +110h, as S4 passes it) and bass detector (native size).
struct Fixture {
    std::vector<uint8_t> song = std::vector<uint8_t>(0x200);
    std::vector<uint8_t> det = std::vector<uint8_t>(rsxrp::kDetSize);

    explicit Fixture(int instrument) {
        Put<int>(rsxr::kDetInstrument, instrument);
        Put<int>(rsxr::kDetFloor, rsxrp::kStockFloor);
        Put<int>(rsxr::kDetLowestOpen, 28);
        Put<int16_t>(rsxr::kDetTuning + 8, -1);  // t4: B0 tuned to Bb0
    }

    template <class T>
    void Put(uint32_t off, T v) {
        *reinterpret_cast<T*>(&det[off]) = v;
    }

    template <class T>
    T Get(uint32_t off) const {
        return *reinterpret_cast<const T*>(&det[off]);
    }

    uint32_t Song(int blockInstrument, int strings) {
        if (blockInstrument >= 0) {
            auto b = std::make_shared<rsxrext::Block>();

            b->instrument = blockInstrument;
            b->stringCount = strings;
            RsxrExt_Store(song.data(), b);
        } else {
            RsxrExt_Store(song.data(), nullptr);
        }

        return RsxrBass_OnSong(det.data(), song.data() + 0x110, Get<int>(rsxr::kDetInstrument));
    }
};
}  // namespace

TEST(bass_mode_binds_the_detector_and_lowers_its_floor) {
    Fixture f(2);

    RsxrPitch_DetFloor(f.det.data(), 1);  // PR0: the reset's floor is this detector's own
    CHECK_EQ(f.Get<int>(rsxr::kDetFloor), rsxrp::kStockFloor);
    CHECK_EQ(f.Song(rsxrext::kInstrumentBass, 5), 5u);
    CHECK_EQ(g_rsxr_bass_strings, 5u);
    CHECK_EQ(RsxrBass_DetectorStrings(f.det.data()), 5u);
    CHECK_EQ(RsxrBass_DetectorStrings(f.song.data()), 0u);
    RsxrPitch_BassDetector(f.det.data());
    RsxrPitch_DetFloor(f.det.data(), 0);
    CHECK_EQ(f.Get<int>(rsxr::kDetLowestOpen), 22);      // 23 - 1
    CHECK_EQ(f.Get<int>(rsxr::kDetFloor), 21);           // one below the lowest open string
    CHECK_EQ(f.Get<int16_t>(rsxrp::kDetOpen6 + 8), 22);  // string 4 open MIDI
    CHECK_EQ(f.Get<int16_t>(rsxrp::kDetOpen6 + 10), 0);  // slot 5 untouched on five strings
    // The next song is a stock 4-string bass (SetTuning: E1 again): the mode clears and the floor goes back to stock.
    CHECK_EQ(f.Song(-1, 0), 0u);
    CHECK_EQ(g_rsxr_bass_strings, 0u);
    f.Put<int>(rsxr::kDetLowestOpen, 28);
    RsxrPitch_BassDetector(f.det.data());
    RsxrPitch_DetFloor(f.det.data(), 0);
    CHECK_EQ(f.Get<int>(rsxr::kDetFloor), rsxrp::kStockFloor);
    RsxrExt_Store(f.song.data(), nullptr);
}

TEST(bass_mode_leaves_guitar_detectors_stock) {
    Fixture f(1);

    CHECK_EQ(f.Song(rsxrext::kInstrumentBass, 6), 0u);
    RsxrPitch_BassDetector(f.det.data());
    CHECK_EQ(f.Get<int>(rsxr::kDetFloor), rsxrp::kStockFloor);
    CHECK_EQ(f.Get<int>(rsxr::kDetLowestOpen), 28);
    CHECK_EQ(g_rsxr_bass_strings, 0u);
    RsxrExt_Store(f.song.data(), nullptr);
}

TEST(bass_bend_hold_bridges_dropouts_of_the_low_b) {
    BendHold h;
    int note = 0;
    float v = 1.6f;

    CHECK(HoldBend(h, &note, true, &v, 10.00f) && v == 1.6f);
    v = -29.6f;  // the tracker lost the pitch (-1 - expected)
    CHECK(HoldBend(h, &note, true, &v, 10.02f) && v == 1.6f);
    CHECK(HoldBend(h, &note, false, &v, 10.04f) && v == 1.6f);  // no tracked note this frame
    v = 0.1f;                                                   // back to the unbent pitch for one frame
    CHECK(HoldBend(h, &note, true, &v, 10.06f) && v == 1.6f);
    v = 1.2f;  // a real move down within kBendDrop
    CHECK(HoldBend(h, &note, true, &v, 10.08f) && v == 1.2f);
    v = 2.0f;  // rises are taken at once
    CHECK(HoldBend(h, &note, true, &v, 10.10f) && v == 2.0f);
}

TEST(bass_bend_hold_keeps_the_bend_over_a_long_dropout) {
    BendHold h;
    int note = 0;
    float v = 1.0f;

    CHECK(HoldBend(h, &note, true, &v, 3.0f) && v == 1.0f);

    for (float t = 3.02f; t < 5.0f; t += 0.02f) {  // a decaying low B: two seconds without a pitch
        v = -1.0f;
        CHECK(HoldBend(h, &note, t < 4.0f, &v, t) && v == 1.0f);
    }

    CHECK(h.missed > 90 && h.longestGap > 1.9f);
}

TEST(bass_bend_hold_lets_a_lasting_release_through) {
    BendHold h;
    int note = 0;
    float v = 2.0f;

    CHECK(HoldBend(h, &note, true, &v, 5.0f));
    v = 0.0f;
    CHECK(HoldBend(h, &note, true, &v, 5.1f) && v == 2.0f);
    v = 0.0f;
    CHECK(HoldBend(h, &note, true, &v, 5.15f) && v == 2.0f && h.low == 2);
    v = 0.0f;  // still down kBendReleaseSec after it fell: the release is real
    CHECK(HoldBend(h, &note, true, &v, 5.1f + kBendReleaseSec + 0.01f) && v == 0.0f);
    v = -1.0f;
    CHECK(HoldBend(h, &note, false, &v, 6.0f) && v == 0.0f);  // a dropout now keeps the released string down
}

TEST(bass_bend_hold_starts_over_for_a_new_note) {
    BendHold h;
    int a = 0, b = 0;
    float v = 2.0f;

    CHECK(HoldBend(h, &a, true, &v, 1.0f));
    v = -1.0f;
    CHECK(!HoldBend(h, &b, false, &v, 1.01f) && v == -1.0f);  // nothing of note a carries over
    v = 0.3f;
    CHECK(HoldBend(h, &b, true, &v, 1.02f) && v == 0.3f);
    v = -1.0f;
    CHECK(HoldBend(h, &b, true, &v, 0.5f) && v == -1.0f);  // the clock went back (restart): no hold
}

TEST(bass_players_keep_each_detector_in_coop) {
    Players t{};
    int bassDet = 0, guitarDet = 0;

    SetupPlayer(t, &bassDet, 2, 6, 1000);
    SetupPlayer(t, &guitarDet, 0, 0, 1005);  // player 2's guitar setup right after: the bass keeps its mode
    CHECK_EQ(StringsOf(t, &bassDet), 6u);
    CHECK_EQ(StringsOf(t, &guitarDet), 0u);
    CHECK_EQ(MaxStrings(t), 6u);
    CHECK(AnyGuitar(t));

    SetupPlayer(t, &bassDet, 2, 5, 9000);  // next song, the bass player alone: player 2's old setup goes
    CHECK_EQ(StringsOf(t, &bassDet), 5u);
    CHECK_EQ(StringsOf(t, &guitarDet), 0u);
    CHECK(!AnyGuitar(t));

    SetupPlayer(t, &bassDet, 2, 0, 9500);  // a stock bass on the same detector clears it
    CHECK_EQ(MaxStrings(t), 0u);
}

TEST(bass_players_two_basses_and_a_full_table) {
    Players t{};
    int a = 0, b = 0, c = 0;

    SetupPlayer(t, &a, 2, 5, 100);
    SetupPlayer(t, &b, 2, 6, 101);
    CHECK_EQ(MaxStrings(t), 6u);
    CHECK(!AnyGuitar(t));
    SetupPlayer(t, &c, 2, 0, 102);  // a third detector takes the oldest slot
    CHECK_EQ(StringsOf(t, &a), 0u);
    CHECK_EQ(StringsOf(t, &b), 6u);
    CHECK_EQ(StringsOf(t, nullptr), 0u);
}

extern "C" void __cdecl Rsxr_Mid_I1(RsxrMid::Context& c);
extern "C" uintptr_t g_rsxr_resume_I1;

TEST(bass_mistake_fret_of_the_low_b_and_high_c) {
    rsxrtest::Restore restoreStrings(g_rsxr_bass_strings);
    rsxrtest::Restore restoreResume(g_rsxr_resume_I1);
    const int16_t tuning[6] = {-2, -2, -2, -2, -2, -1};  // a step down, high C a half step down
    RsxrMid::Context c{};

    g_rsxr_resume_I1 = 0x1000;
    g_rsxr_bass_strings = 6;

    // Low B (base 23) tuned to A (-2): MIDI 26 is fret 5.
    c.eax = 2;
    c.ecx = 26;
    c.edx = 4;
    c.esi = uint32_t(reinterpret_cast<uintptr_t>(tuning));
    Rsxr_Mid_I1(c);
    CHECK_EQ(c.eax, 5u);
    CHECK_EQ(c.ecx, 5u);
    CHECK_EQ(c.jump, 0x100Du);

    // High C (base 48) tuned to B (-1): MIDI 50 is fret 3.
    c = RsxrMid::Context{};
    c.eax = 2;
    c.ecx = 50;
    c.edx = 5;
    c.esi = uint32_t(reinterpret_cast<uintptr_t>(tuning));
    Rsxr_Mid_I1(c);
    CHECK_EQ(c.eax, 3u);
    CHECK_EQ(c.jump, 0x100Du);

    // A 5-string bass has no string 5, a stock string stays stock: the game's code runs.
    for (const uint32_t strings : {5u, 0u}) {
        g_rsxr_bass_strings = strings;
        c = RsxrMid::Context{};
        c.eax = 2;
        c.ecx = 50;
        c.edx = strings ? 5 : 4;
        c.esi = uint32_t(reinterpret_cast<uintptr_t>(tuning));
        Rsxr_Mid_I1(c);
        CHECK_EQ(c.jump, 0u);
        CHECK_EQ(c.ecx, 50u);
    }

    g_rsxr_bass_strings = 6;
    c = RsxrMid::Context{};
    c.eax = 2;
    c.ecx = 40;
    c.edx = 2;
    c.esi = uint32_t(reinterpret_cast<uintptr_t>(tuning));
    Rsxr_Mid_I1(c);
    CHECK_EQ(c.jump, 0u);
}
