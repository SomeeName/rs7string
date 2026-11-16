#include "features/pitch/pitch.h"
#include "features/tuner/tuner.h"
#include "model/instrument.h"
#include "test.h"
#include "testutil.h"
#include <cstdint>
#include <cstring>
#include <vector>

// The per-detector song tables in features/pitch/hooks.cpp (the song count and the kept 7 / 8-entry song tuning) hold
// rs7::kMaxGuitars entries (K). A full table replaces its entries in turn: the K newest detectors survive, the newest
// one is never the one the next insert evicts. Other tests leave stale entries behind (freed detectors), so each test
// first clears the entries of its own addresses and relies only on "after at least K inserts into a full table".
namespace {
size_t g_fakeSize = 0;

size_t __cdecl FakeMsize(void*) {
    return g_fakeSize;
}

struct MsizeScope {  // g_rs7_fn_msize -> FakeMsize reporting `size`, restored afterwards
    uintptr_t saved = g_rs7_fn_msize;

    explicit MsizeScope(size_t size) {
        g_fakeSize = size;
        g_rs7_fn_msize = reinterpret_cast<uintptr_t>(&FakeMsize);
    }

    ~MsizeScope() { g_rs7_fn_msize = saved; }
};

template <class T>
T Get(const std::vector<uint8_t>& b, uint32_t off) {
    T v;

    std::memcpy(&v, &b[off], sizeof(T));

    return v;
}

template <class T>
void Put(std::vector<uint8_t>& b, uint32_t off, T v) {
    std::memcpy(&b[off], &v, sizeof(T));
}
}  // namespace

TEST(pitch_song_count_table_full_replaces_in_turn_and_keeps_the_newest) {
    constexpr unsigned K = rs7::kMaxGuitars;
    static const uint8_t dets[2 * K + 1][4] = {};  // only the addresses are used

    for (const auto& d : dets) {
        Rs7Pitch_SetSongCount(d, 0);  // a stale entry of this address (another test) goes
    }

    for (unsigned i = 0; i < 2 * K; ++i) {
        Rs7Pitch_SetSongCount(dets[i], 7);
    }

    // Before the round robin fix every insert into the full table replaced entry 0: dK..d(2K-2) were lost.
    for (unsigned i = 0; i < K; ++i) {
        CHECK_EQ(Rs7Pitch_SongCount(dets[i]), 0u);
    }

    for (unsigned i = K; i < 2 * K; ++i) {
        CHECK_EQ(Rs7Pitch_SongCount(dets[i]), 7u);
    }

    Rs7Pitch_SetSongCount(dets[2 * K], 8);  // the next one evicts the oldest (dK), not the newest (d(2K-1))
    CHECK_EQ(Rs7Pitch_SongCount(dets[K]), 0u);

    for (unsigned i = K + 1; i < 2 * K; ++i) {
        CHECK_EQ(Rs7Pitch_SongCount(dets[i]), 7u);
    }

    CHECK_EQ(Rs7Pitch_SongCount(dets[2 * K]), 8u);

    for (const auto& d : dets) {
        Rs7Pitch_SetSongCount(d, 0);
    }
}

TEST(pitch_song_tuning_table_full_replaces_in_turn_and_keeps_the_newest) {
    constexpr size_t K = rs7::kMaxGuitars;
    MsizeScope msize(rs7p::kDetSize);
    rs7test::Restore restore_g_rs7_native_tuner(g_rs7_native_tuner);

    g_rs7_native_tuner = 1;  // a profile re-apply keeps a remembered song's extra strings
    std::vector<std::vector<uint8_t>> dets(2 * K, std::vector<uint8_t>(rs7p::kDetSize, 0));

    for (auto& det : dets) {
        Put<int>(det, rs7::kDetLowestOpen, 40);
        Rs7Pitch_DetReset(det.data());  // drops a stale song entry of this address (another test)
    }

    const int16_t song[7] = {0, 0, 0, 0, 0, 0, -1};  // 7-string, low A#
    const Rs7Int16Vec v7{song, song + 7};

    for (auto& det : dets) {
        Rs7Pitch_DetTuned(det.data(), &v7);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), -1);
    }

    const int16_t profile[6] = {};
    const Rs7Int16Vec v6{profile, profile + 6};

    // The profile re-apply keeps string 6 only for a detector whose song entry survived. Before the fix every insert
    // into the full table replaced entry 0, so dK..d(2K-2) lost theirs and the re-apply zeroed their string 6.
    for (size_t i = 0; i < dets.size(); ++i) {
        Put<int>(dets[i], rs7::kDetLowestOpen, 40);
        g_rs7_profile_apply = 1;
        Rs7Pitch_DetTuned(dets[i].data(), &v6);
        g_rs7_profile_apply = 0;
        CHECK_EQ(Get<int16_t>(dets[i], rs7p::kDetTuning8 + 12), int16_t(i < K ? 0 : -1));
    }

    for (auto& det : dets) {
        Rs7Pitch_DetReset(det.data());
    }
}
