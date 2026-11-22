#include "features/nslot/nslot.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "test.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <memory>

// leaks2 (docs/ARCHITECTURE.md): the guitar-free mode cleanup and the extension block store's cap.

TEST(leaks2_free_clears_song_marks_even_without_a_strings_entry) {
    alignas(4) uint8_t guitar[0x50] = {}, unrelated[0x50] = {}, strings[0x40] = {};
    void* owner = guitar;

    std::memcpy(strings, &owner, sizeof owner);  // GuitarStrings +0 is the owner
    void* gs = strings;

    std::memcpy(guitar + 0xC, &gs, sizeof gs);
    RsxrS7_SetGuitarMode(guitar, true);
    RsxrS7_SetGuitarMode(unrelated, true);
    RsxrS7Stats before{}, after{};

    RsxrS7_Stats(&before);
    Rsxr_S7Free(strings);
    RsxrS7_Stats(&after);
    CHECK_EQ(after.sevenGuitars, before.sevenGuitars - 1);
    Rsxr_S7Free(strings);  // repeated free must leave the unrelated entry intact
    RsxrS7_Stats(&before);
    CHECK_EQ(after.sevenGuitars, before.sevenGuitars);
    RsxrS7_SetGuitarMode(unrelated, false);
}

TEST(leaks2_extblock_store_is_capped_oldest_dropped_newest_kept) {
    // Big test (tests/big-test/R7_allocstats.txt): extBlocks rose 1 per chord-chart load. 12 loads here.
    static uint8_t songs[12];
    const int base = RsxrExt_StoredBlocks();

    for (int i = 0; i < 12; ++i) {
        RsxrExt_Store(&songs[i], std::make_shared<rsxrext::Block>());
        CHECK(RsxrExt_StoredBlocks() <= kRsxrExtKeep);
        CHECK(RsxrExt_Find(&songs[i]) != nullptr);  // the song just loaded (the one played) is kept
    }

    CHECK(base <= kRsxrExtKeep);
    CHECK_EQ(RsxrExt_StoredBlocks(), kRsxrExtKeep);

    for (int i = 0; i < 12 - kRsxrExtKeep; ++i) {
        CHECK(RsxrExt_Find(&songs[i]) == nullptr);  // oldest dropped
    }

    for (int i = 12 - kRsxrExtKeep; i < 12; ++i) {
        CHECK(RsxrExt_Find(&songs[i]) != nullptr);
    }

    // Reloading a kept song moves it to the newest end: the next load drops songs[5], not it.
    RsxrExt_Store(&songs[4], std::make_shared<rsxrext::Block>());
    CHECK(RsxrExt_Find(&songs[4]) != nullptr);
    CHECK_EQ(RsxrExt_StoredBlocks(), kRsxrExtKeep);
    RsxrExt_Store(&songs[0], std::make_shared<rsxrext::Block>());
    CHECK(RsxrExt_Find(&songs[5]) == nullptr);
    CHECK(RsxrExt_Find(&songs[4]) != nullptr);
    CHECK(RsxrExt_Find(&songs[0]) != nullptr);

    for (int i = 0; i < 12; ++i) {
        RsxrExt_Store(&songs[i], nullptr);
    }

    CHECK(RsxrExt_StoredBlocks() <= base);
}
