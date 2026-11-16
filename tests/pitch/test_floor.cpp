#include "features/pitch/pitch.h"
#include "test.h"
#include <cstdint>
#include <vector>

// The detector's raw floor follows the song's lowest open string (rs7p::Floor, Rs7Pitch_DetFloor).
TEST(floor_stays_stock_down_to_c1) {
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 40), 24);  // E standard guitar
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 28), 24);  // E standard bass
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 24), 24);  // C standard bass: stock
}

TEST(floor_goes_one_below_a_lower_open_string) {
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 23), 22);  // B0, 5-string bass
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 21), 20);  // A0, drop A / A standard 5-string bass
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 19), 18);  // G0, drop G 5-string bass: the lowest that fits
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 18), 18);  // F#0 (8-string F#1 an octave down): clamped
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, rs7p::kMinFloor), rs7p::kMinFloor);
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, 0), rs7p::kMinFloor);     // never below the buffer's range
    CHECK_EQ(rs7p::Floor(rs7p::kStockFloor, -300), rs7p::kMinFloor);  // garbage tuning
}

TEST(floor_keeps_a_lower_reset_floor_of_another_mod) {
    CHECK_EQ(rs7p::Floor(18, 28), 18);  // RSMods' low tuning fix patches the reset to 18
    CHECK_EQ(rs7p::Floor(18, 16), 18);
    CHECK_EQ(rs7p::Floor(20, 19), 18);
    CHECK_EQ(rs7p::Floor(10, 3), 10);  // a reset floor below ours is never raised
}

namespace {
struct Det {
    std::vector<uint8_t> mem = std::vector<uint8_t>(rs7p::kDetSize);

    int& At(uint32_t off) { return *reinterpret_cast<int*>(&mem[off]); }

    void Reset(int floor) {  // Play_Detector_Reset, then PR0
        At(rs7::kDetFloor) = floor;
        Rs7Pitch_DetFloor(mem.data(), 1);
    }

    int Tune(int lowest) {  // SetTuning (lowest open MIDI), then PT1 / S3 / S4
        At(rs7::kDetLowestOpen) = lowest;
        Rs7Pitch_DetFloor(mem.data(), 0);

        return At(rs7::kDetFloor);
    }
};
}  // namespace

TEST(det_floor_lowers_and_restores_per_detector) {
    Det a, b;

    a.Reset(rs7p::kStockFloor);
    b.Reset(18);  // another mod's floor
    CHECK_EQ(a.Tune(21), 20);
    CHECK_EQ(b.Tune(21), 18);
    CHECK_EQ(a.Tune(28), 24);  // the next song: back to the reset's floor
    CHECK_EQ(b.Tune(28), 18);
    CHECK_EQ(a.Tune(23), 22);
    a.Reset(rs7p::kStockFloor);  // reset rewrites the floor; the record follows
    CHECK_EQ(a.Tune(40), 24);
}

TEST(det_floor_records_a_detector_first_seen_after_its_reset) {
    Det d;

    d.At(rs7::kDetFloor) = rs7p::kStockFloor;  // reset ran before the hook was written
    CHECK_EQ(d.Tune(21), 20);
    CHECK_EQ(d.Tune(28), 24);
    Rs7Pitch_DetFloor(nullptr, 0);  // no detector: nothing happens
}
