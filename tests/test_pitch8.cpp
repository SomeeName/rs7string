#include "core/mode.h"
#include "core/pitch8.h"
#include "test.h"
#include <cstdint>
#include <vector>

// Native 8-entry tuning / open-MIDI storage (core/pitch8.h, issue #18 stage 5 area 3).
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
    return *reinterpret_cast<const T*>(&b[off]);
}

template <class T>
void Put(std::vector<uint8_t>& b, uint32_t off, T v) {
    *reinterpret_cast<T*>(&b[off]) = v;
}
}  // namespace

TEST(pitch8_fill_tuning_copies_up_to_eight_and_zeroes_the_rest) {
    const int16_t v[9] = {-2, 1, 2, 3, 4, 5, -1, 7, 99};
    int16_t t[8];

    rs7p::FillTuning(v, 6, t);

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(t[i], i < 6 ? v[i] : 0);
    }

    rs7p::FillTuning(v, 9, t);

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(t[i], v[i]);
    }
}

TEST(pitch8_open_midi_is_the_stock_unrolled_formula) {
    const int16_t t[8] = {-2, -1, 0, 1, 2, 3, -1, 4};
    int16_t g[8], b[8];

    rs7p::FillOpen(t, false, g);
    const int gb[8] = {40, 45, 50, 55, 59, 64, 35, 30};

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(g[i], gb[i] + t[i]);
    }

    rs7p::FillOpen(t, true, b);
    const int bb[4] = {28, 33, 38, 43};

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(b[i], i < 4 ? bb[i] + t[i] : t[i]);  // bass 4/5 raw like 0x4E2590
    }
}

TEST(pitch8_lowest_matches_s3_for_seven_and_extends_to_eight) {
    const int16_t t[8] = {0, 0, 0, 0, 0, 0, -2, -1};

    CHECK_EQ(rs7p::Lowest(40, t, 6, false), 40);
    CHECK_EQ(rs7p::Lowest(40, t, 7, false), 33);  // S3: 35 + t6
    CHECK_EQ(rs7p::Lowest(40, t, 8, false), 29);  // + 30 + t7
    CHECK_EQ(rs7p::Lowest(20, t, 8, false), 20);  // never raised
    CHECK_EQ(rs7p::Lowest(28, t, 8, true), 28);   // bass untouched
}

TEST(pitch8_det_tuned_writes_the_tail_only_into_a_grown_block) {
    std::vector<uint8_t> det(rs7p::kDetSize, 0xCD);

    Put<int>(det, rs7::kDetInstrument, 1);
    Put<int>(det, rs7::kDetLowestOpen, 40);
    std::vector<int16_t> v = {0, 0, 0, 0, 0, 0, -2};
    const Rs7Int16Vec vec{v.data(), v.data() + v.size()};

    {
        MsizeScope ms(rs7p::kDetStockSize);  // allocated before PA1: untouched

        Rs7Pitch_DetTuned(det.data(), &vec);
        CHECK_EQ(Get<uint32_t>(det, rs7p::kDetMagic), 0xCDCDCDCDu);
        CHECK_EQ(Get<int>(det, rs7::kDetLowestOpen), 40);
    }

    {
        MsizeScope ms(rs7p::kDetSize);

        Rs7Pitch_DetTuned(det.data(), &vec);
        CHECK_EQ(Get<uint32_t>(det, rs7p::kDetMagic), rs7p::kMagic);
        CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 6u);  // the active count stays 6
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), -2);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 14), 0);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetOpen8 + 12), 33);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetOpen8 + 14), 30);
        CHECK_EQ(Get<int>(det, rs7::kDetLowestOpen), 33);
    }

    g_rs7_fn_msize = 0;  // no _msize: never written
    std::vector<uint8_t> m(rs7p::kMatSize, 0xCD);

    Rs7Pitch_MatNew(m.data());
    CHECK_EQ(Get<uint32_t>(m, rs7p::kMatMagic), 0xCDCDCDCDu);
}

TEST(pitch8_matcher_tail) {
    std::vector<uint8_t> m(rs7p::kMatSize, 0xCD);
    MsizeScope ms(rs7p::kMatSize);

    Rs7Pitch_MatNew(m.data());
    CHECK_EQ(Get<uint32_t>(m, rs7p::kMatMagic), rs7p::kMagic);

    for (uint32_t i = 0; i < 8; ++i) {
        CHECK_EQ(Get<int16_t>(m, rs7p::kMatTuning8 + 2 * i), 0);
    }

    std::vector<int16_t> v = {-2, 0, 0, 0, 0, 0, -1, 3};
    const Rs7Int16Vec vec{v.data(), v.data() + v.size()};

    Rs7Pitch_MatTuned(m.data(), &vec);

    for (uint32_t i = 0; i < 8; ++i) {
        CHECK_EQ(Get<int16_t>(m, rs7p::kMatTuning8 + 2 * i), v[i]);
    }
}

TEST(pitch8_tailed_needs_the_native_block_size_before_the_magic) {
    std::vector<uint8_t> m(rs7p::kMatSize, 0), d(rs7p::kDetSize, 0);

    Put<uint32_t>(m, rs7p::kMatMagic, rs7p::kMagic);
    Put<uint32_t>(d, rs7p::kDetMagic, rs7p::kMagic);

    {
        MsizeScope ms(rs7p::kMatStockSize);  // stock-size block: the magic past it is never trusted

        CHECK_EQ(Rs7Pitch_Tailed(m.data(), 0), 0);
    }

    {
        MsizeScope ms(rs7p::kDetStockSize);

        CHECK_EQ(Rs7Pitch_Tailed(d.data(), 1), 0);
    }

    {
        MsizeScope ms(rs7p::kDetSize);

        CHECK_EQ(Rs7Pitch_Tailed(m.data(), 0), 1);
        CHECK_EQ(Rs7Pitch_Tailed(d.data(), 1), 1);
        CHECK_EQ(Rs7Pitch_Tailed(nullptr, 0), 0);
        Put<uint32_t>(m, rs7p::kMatMagic, 0);
        CHECK_EQ(Rs7Pitch_Tailed(m.data(), 0), 0);
    }
}

#include "core/tuner8.h"

// [neck] native_tuner: a six-entry profile re-apply (Play_Tuning_ApplyToDetection, TS8PA..TS8PZ)
// keeps the song's strings 6 / 7 in the detector / matcher tails; without the switch (or outside the
// re-apply) six entries zero them as before.
TEST(pitch8_profile_reapply_keeps_the_song_strings_with_native_tuner) {
    MsizeScope msize(rs7p::kDetSize);
    const uintptr_t savedTuner = g_rs7_native_tuner;

    for (int nativeTuner : {0, 1}) {
        g_rs7_native_tuner = uintptr_t(nativeTuner);
        std::vector<uint8_t> det(rs7p::kDetSize, 0);

        Put<int>(det, rs7::kDetLowestOpen, 40);
        Rs7Pitch_DetReset(det.data());
        const int16_t song[7] = {0, 0, 0, 0, 0, 0, -1};  // 7-string, low A# (34)
        const Rs7Int16Vec v7{song, song + 7};

        Rs7Pitch_DetTuned(det.data(), &v7);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), -1);
        CHECK_EQ(Get<int>(det, rs7::kDetLowestOpen), 34);
        const int16_t profile[6] = {0, 0, 0, 0, 0, 0};
        const Rs7Int16Vec v6{profile, profile + 6};

        Put<int>(det, rs7::kDetLowestOpen, 40);                   // the stock SetTuning: string 0 + 40
        g_rs7_profile_apply = Rs7Pitch_KeepSongTuning() ? 1 : 0;  // TS8PA (written only with the switch)
        Rs7Pitch_DetTuned(det.data(), &v6);
        g_rs7_profile_apply = 0;  // TS8PZ
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), nativeTuner ? -1 : 0);
        CHECK_EQ(Get<int>(det, rs7::kDetLowestOpen), nativeTuner ? 34 : 40);
        // A six-string song (outside the re-apply) replaces it in both modes.
        Put<int>(det, rs7::kDetLowestOpen, 40);
        Rs7Pitch_DetTuned(det.data(), &v6);
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), 0);
        g_rs7_profile_apply = Rs7Pitch_KeepSongTuning() ? 1 : 0;
        Rs7Pitch_DetTuned(det.data(), &v6);
        g_rs7_profile_apply = 0;
        CHECK_EQ(Get<int16_t>(det, rs7p::kDetTuning8 + 12), 0);
        CHECK_EQ(Get<int>(det, rs7::kDetLowestOpen), 40);
    }

    g_rs7_native_tuner = savedTuner;
}

TEST(pitch8_song_count_survives_the_set_tuning_and_ends_with_the_detector) {
    std::vector<uint8_t> det(rs7p::kDetSize, 0xCD);

    Put<int>(det, rs7::kDetInstrument, 1);
    Put<int>(det, rs7::kDetLowestOpen, 40);
    std::vector<int16_t> v = {0, 0, 0, 0, 0, 0, -2};
    const Rs7Int16Vec vec{v.data(), v.data() + v.size()};
    MsizeScope ms(rs7p::kDetSize);

    CHECK_EQ(Rs7Pitch_SongCount(det.data()), 0u);
    Rs7Pitch_SetSongCount(det.data(), 7);  // S4 (Rs7Song_WidenDetector): the song is 7 strings
    CHECK_EQ(Rs7Pitch_SongCount(det.data()), 7u);
    Rs7Pitch_DetTuned(det.data(), &vec);  // the SetTuning that S4 runs next
    CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 7u);
    v.resize(6);
    const Rs7Int16Vec six{v.data(), v.data() + v.size()};

    Rs7Pitch_DetTuned(det.data(), &six);  // a profile re-apply mid-song keeps it
    CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 7u);
    Rs7Pitch_SetSongCount(det.data(), 0);  // the next S4 of a 6-string song
    Rs7Pitch_DetTuned(det.data(), &six);
    CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 6u);
    Rs7Pitch_SetSongCount(det.data(), 8);
    Rs7Pitch_DetReset(det.data());  // PR0: a new / reset detector has no song
    CHECK_EQ(Rs7Pitch_SongCount(det.data()), 0u);
    CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 6u);
    Put<int>(det, rs7::kDetInstrument, 2);  // bass never widens
    Rs7Pitch_SetSongCount(det.data(), 7);
    Rs7Pitch_DetTuned(det.data(), &six);
    CHECK_EQ(Get<uint32_t>(det, rs7p::kDetCount), 4u);
    Rs7Pitch_SetSongCount(det.data(), 0);
}
