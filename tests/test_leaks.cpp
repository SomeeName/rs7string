// Leak audit (README, [debug] alloc_stats): the lifetime of what the DLL allocates, per song.
#include "features/allocstats/allocstats.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
struct FakeSong {
    alignas(16) uint8_t b[0x200] = {};
    std::vector<uint8_t> tpl, cn, tuning;

    FakeSong(uint32_t templates, uint32_t chordNotes)
        : tpl(templates * rsxrns::kTplSize + 1), cn(chordNotes * rsxrns::kCnSize + 1), tuning(12) {
        auto vec = [&](uint32_t off, std::vector<uint8_t>& v, size_t used) {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(v.data());
            const uintptr_t end = begin + used;

            std::memcpy(b + off, &begin, 4);
            std::memcpy(b + off + 4, &end, 4);
        };

        vec(0x94, tpl, templates * rsxrns::kTplSize);
        vec(0xAC, cn, chordNotes * rsxrns::kCnSize);
        vec(0x110, tuning, 12);
        vec(0x40, tpl, 0);  // no levels
    }
};
}  // namespace

TEST(allocstats_counts_live_and_total_and_logs_every_kind) {
    RsxrAlloc_ResetForTests();
    RsxrAlloc_Inc(kAkGuitarStrings);
    RsxrAlloc_Inc(kAkGuitarStrings);
    RsxrAlloc_Dec(kAkGuitarStrings);
    CHECK_EQ(RsxrAlloc_Live(kAkGuitarStrings), 1);
    CHECK_EQ(RsxrAlloc_Total(kAkGuitarStrings), 2);
    CHECK_EQ(RsxrAlloc_Live(kAkCount), 0);  // out of range: ignored
    RsxrAlloc_Inc(kAkCount);
    const std::string line = RsxrAlloc_Line("song start", 3);

    CHECK(line.find("allocstats song start #3:") == 0);

    for (int k = 0; k < kAkCount; ++k) {
        CHECK(line.find(std::string(" ") + RsxrAlloc_Name(RsxrAllocKind(k)) + "=") != std::string::npos);
    }

    CHECK(line.find(" gs=1/2 ") != std::string::npos);
    CHECK(line.find("freeVA=") != std::string::npos);
    CHECK(line.find("nsSongs=") != std::string::npos);
    RsxrAlloc_SetEnabledForTests(false);
    RsxrAlloc_Snapshot("song start");  // disabled: nothing, no throw
    RsxrAlloc_ResetForTests();
}

TEST(sidestore_does_not_keep_inactive_entries_and_caps_active_ones) {
    RsxrStore_ClearAll();
    // A song per object address, inactive (6-string): nothing stays behind.
    static uint8_t keys[512];

    for (int i = 0; i < 300; ++i) {
        RsxrStore_Set(&keys[i], RsxrEntry{false, 0});
    }

    CHECK_EQ(RsxrStore_Count(), 0);
    RsxrEntry e{};

    CHECK(!RsxrStore_Get(&keys[0], &e));
    // An active entry is overwritten by the next inactive write of the same key (address reuse).
    RsxrStore_Set(&keys[1], RsxrEntry{true, 4});
    CHECK(RsxrStore_Get(&keys[1], &e) && e.active && e.t6 == 4);
    RsxrStore_Set(&keys[1], RsxrEntry{false, 0});
    CHECK(!RsxrStore_Get(&keys[1], &e));
    CHECK(!RsxrStore_AnyActive());

    // 7-string songs on ever new addresses: the store never exceeds its cap, the newest entry is kept.
    for (int i = 0; i < 300; ++i) {
        RsxrStore_Set(&keys[i], RsxrEntry{true, int16_t(i % 7)});
    }

    CHECK(size_t(RsxrStore_Count()) <= kRsxrStoreMax);
    CHECK(RsxrStore_Get(&keys[299], &e) && e.active && e.t6 == 299 % 7);
    RsxrStore_ClearAll();
}

TEST(assetload_head_slots_are_released_with_the_guitar) {
    rsxrtest::Restore restore_g_rsxr_asset_on(g_rsxr_asset_on);

    g_rsxr_asset_on = 1;
    RsxrAlloc_ResetForTests();
    // 40 guitars one after the other (a new object address per song). Each is released before the next.
    static uint8_t strings[40][16];
    static uint8_t guitars[40][0x40];

    for (int i = 0; i < 40; ++i) {
        uint8_t* gs = strings[i];

        std::memset(guitars[i], 0, sizeof guitars[i]);
        std::memcpy(guitars[i] + 0x0C, &gs, 4);
        RsxrAsset_HeadFrame(guitars[i], false);
        CHECK_EQ(RsxrAsset_LiveHeads(), 1);
        RsxrAsset_ForgetStrings(gs);
        CHECK_EQ(RsxrAsset_LiveHeads(), 0);
    }

    CHECK_EQ(RsxrAlloc_Live(kAkHeadSlots), 0);
    CHECK_EQ(RsxrAlloc_Total(kAkHeadSlots), 40);

    // Not released: the table is full after 8 and the 9th guitar gets no slot (the old behaviour).
    for (int i = 0; i < 12; ++i) {
        RsxrAsset_HeadFrame(guitars[i], false);
    }

    CHECK_EQ(RsxrAsset_LiveHeads(), 8);

    for (int i = 0; i < 12; ++i) {
        RsxrAsset_ForgetStrings(strings[i]);
    }

    CHECK_EQ(RsxrAsset_LiveHeads(), 0);
    RsxrAsset_ForgetStrings(nullptr);
    RsxrAlloc_ResetForTests();
}

TEST(nslot_t8_c8_follow_the_song_object) {
    RsxrAlloc_ResetForTests();
    const int base = RsxrNs_LiveSongs();  // other tests may have left songs registered

    for (int round = 0; round < 50; ++round) {
        FakeSong song(5, 2);

        RsxrNs_OnSngLoaded(song.b);
        CHECK_EQ(RsxrAlloc_Live(kAkNsT8), 1);
        RsxrNs_OnPostProcessed(song.b);
        RsxrNs_OnPostProcessed(song.b);  // PostProcess rebuilds in place: no second block
        CHECK_EQ(RsxrAlloc_Live(kAkNsT8), 1);
        CHECK_EQ(RsxrAlloc_Live(kAkNsC8), 1);
        CHECK_EQ(RsxrNs_LiveSongs(), base + 1);
        RsxrNs_OnSngLoaded(song.b);  // the same address again (a recycled song object)
        CHECK_EQ(RsxrAlloc_Live(kAkNsT8), 1);
        CHECK_EQ(RsxrAlloc_Live(kAkNsC8), 0);  // dropped with the old entry, rebuilt by PostProcess
        RsxrNs_OnPostProcessed(song.b);
        RsxrNs_OnSongFree(song.b);
        CHECK_EQ(RsxrAlloc_Live(kAkNsT8), 0);
        CHECK_EQ(RsxrAlloc_Live(kAkNsC8), 0);
        CHECK_EQ(RsxrNs_LiveSongs(), base);
    }

    CHECK(RsxrAlloc_Total(kAkNsT8) >= 50);
    RsxrAlloc_ResetForTests();
}
