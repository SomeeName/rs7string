// Leak audit (README, [debug] alloc_stats): the lifetime of what the DLL allocates, per song.
#include "features/allocstats/allocstats.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "test.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
struct FakeSong {
    alignas(16) uint8_t b[0x200] = {};
    std::vector<uint8_t> tpl, cn, tuning;

    FakeSong(uint32_t templates, uint32_t chordNotes)
        : tpl(templates * rs7ns::kTplSize + 1), cn(chordNotes * rs7ns::kCnSize + 1), tuning(12) {
        auto vec = [&](uint32_t off, std::vector<uint8_t>& v, size_t used) {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(v.data());
            const uintptr_t end = begin + used;

            std::memcpy(b + off, &begin, 4);
            std::memcpy(b + off + 4, &end, 4);
        };

        vec(0x94, tpl, templates * rs7ns::kTplSize);
        vec(0xAC, cn, chordNotes * rs7ns::kCnSize);
        vec(0x110, tuning, 12);
        vec(0x40, tpl, 0);  // no levels
    }
};
}  // namespace

TEST(allocstats_counts_live_and_total_and_logs_every_kind) {
    Rs7Alloc_ResetForTests();
    Rs7Alloc_Inc(kAkGuitarStrings);
    Rs7Alloc_Inc(kAkGuitarStrings);
    Rs7Alloc_Dec(kAkGuitarStrings);
    CHECK_EQ(Rs7Alloc_Live(kAkGuitarStrings), 1);
    CHECK_EQ(Rs7Alloc_Total(kAkGuitarStrings), 2);
    CHECK_EQ(Rs7Alloc_Live(kAkCount), 0);  // out of range: ignored
    Rs7Alloc_Inc(kAkCount);
    const std::string line = Rs7Alloc_Line("song start", 3);

    CHECK(line.find("allocstats song start #3:") == 0);

    for (int k = 0; k < kAkCount; ++k) {
        CHECK(line.find(std::string(" ") + Rs7Alloc_Name(Rs7AllocKind(k)) + "=") != std::string::npos);
    }

    CHECK(line.find(" gs=1/2 ") != std::string::npos);
    CHECK(line.find("freeVA=") != std::string::npos);
    CHECK(line.find("nsSongs=") != std::string::npos);
    Rs7Alloc_SetEnabledForTests(false);
    Rs7Alloc_Snapshot("song start");  // disabled: nothing, no throw
    Rs7Alloc_ResetForTests();
}

TEST(sidestore_does_not_keep_inactive_entries_and_caps_active_ones) {
    Rs7Store_ClearAll();
    // A song per object address, inactive (6-string): nothing stays behind.
    static uint8_t keys[512];

    for (int i = 0; i < 300; ++i) {
        Rs7Store_Set(&keys[i], Rs7Entry{false, 0});
    }

    CHECK_EQ(Rs7Store_Count(), 0);
    Rs7Entry e{};

    CHECK(!Rs7Store_Get(&keys[0], &e));
    // An active entry is overwritten by the next inactive write of the same key (address reuse).
    Rs7Store_Set(&keys[1], Rs7Entry{true, 4});
    CHECK(Rs7Store_Get(&keys[1], &e) && e.active && e.t6 == 4);
    Rs7Store_Set(&keys[1], Rs7Entry{false, 0});
    CHECK(!Rs7Store_Get(&keys[1], &e));
    CHECK(!Rs7Store_AnyActive());

    // 7-string songs on ever new addresses: the store never exceeds its cap, the newest entry is kept.
    for (int i = 0; i < 300; ++i) {
        Rs7Store_Set(&keys[i], Rs7Entry{true, int16_t(i % 7)});
    }

    CHECK(size_t(Rs7Store_Count()) <= kRs7StoreMax);
    CHECK(Rs7Store_Get(&keys[299], &e) && e.active && e.t6 == 299 % 7);
    Rs7Store_ClearAll();
}

TEST(assetload_head_slots_are_released_with_the_guitar) {
    const uintptr_t was = g_rs7_asset_on;

    g_rs7_asset_on = 1;
    Rs7Alloc_ResetForTests();
    // 40 guitars one after the other (a new object address per song). Each is released before the next.
    static uint8_t strings[40][16];
    static uint8_t guitars[40][0x40];

    for (int i = 0; i < 40; ++i) {
        uint8_t* gs = strings[i];

        std::memset(guitars[i], 0, sizeof guitars[i]);
        std::memcpy(guitars[i] + 0x0C, &gs, 4);
        Rs7Asset_HeadFrame(guitars[i], false);
        CHECK_EQ(Rs7Asset_LiveHeads(), 1);
        Rs7Asset_ForgetStrings(gs);
        CHECK_EQ(Rs7Asset_LiveHeads(), 0);
    }

    CHECK_EQ(Rs7Alloc_Live(kAkHeadSlots), 0);
    CHECK_EQ(Rs7Alloc_Total(kAkHeadSlots), 40);

    // Not released: the table is full after 8 and the 9th guitar gets no slot (the old behaviour).
    for (int i = 0; i < 12; ++i) {
        Rs7Asset_HeadFrame(guitars[i], false);
    }

    CHECK_EQ(Rs7Asset_LiveHeads(), 8);

    for (int i = 0; i < 12; ++i) {
        Rs7Asset_ForgetStrings(strings[i]);
    }

    CHECK_EQ(Rs7Asset_LiveHeads(), 0);
    Rs7Asset_ForgetStrings(nullptr);
    Rs7Alloc_ResetForTests();
    g_rs7_asset_on = was;
}

TEST(nslot_t8_c8_follow_the_song_object) {
    Rs7Alloc_ResetForTests();
    const int base = Rs7Ns_LiveSongs();  // other tests may have left songs registered

    for (int round = 0; round < 50; ++round) {
        FakeSong song(5, 2);

        Rs7Ns_OnSngLoaded(song.b);
        CHECK_EQ(Rs7Alloc_Live(kAkNsT8), 1);
        Rs7Ns_OnPostProcessed(song.b);
        Rs7Ns_OnPostProcessed(song.b);  // PostProcess rebuilds in place: no second block
        CHECK_EQ(Rs7Alloc_Live(kAkNsT8), 1);
        CHECK_EQ(Rs7Alloc_Live(kAkNsC8), 1);
        CHECK_EQ(Rs7Ns_LiveSongs(), base + 1);
        Rs7Ns_OnSngLoaded(song.b);  // the same address again (a recycled song object)
        CHECK_EQ(Rs7Alloc_Live(kAkNsT8), 1);
        CHECK_EQ(Rs7Alloc_Live(kAkNsC8), 0);  // dropped with the old entry, rebuilt by PostProcess
        Rs7Ns_OnPostProcessed(song.b);
        Rs7Ns_OnSongFree(song.b);
        CHECK_EQ(Rs7Alloc_Live(kAkNsT8), 0);
        CHECK_EQ(Rs7Alloc_Live(kAkNsC8), 0);
        CHECK_EQ(Rs7Ns_LiveSongs(), base);
    }

    CHECK(Rs7Alloc_Total(kAkNsT8) >= 50);
    Rs7Alloc_ResetForTests();
}
