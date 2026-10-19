#include "features/allocstats/allocstats.h"
#include "features/clite/clite.h"
#include "features/headstock/headstock.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "test.h"
#include <cstdint>
#include <cstring>

// fix/escrebuild: Esc in a 7-string pre-song tuner crashed (hs33 .mdmp: the live tuner applied transforms to the
// guitar a mode-entry rebuild had just freed).

TEST(escrebuild_tuner_cancel_is_not_a_mode_entry) {
    using rs7clite::RebuildTarget;

    CHECK(rs7clite::ModeEntry(rs7clite::kRetTunerCancel) == nullptr);
    CHECK_EQ(RebuildTarget(rs7clite::kRetTunerCancel, 0, 6, 7), 0u);
    CHECK_EQ(RebuildTarget(rs7clite::kRetTunerCancel, 0, 6, 8), 0u);
    CHECK_EQ(sizeof rs7clite::kRetModeEntries / sizeof rs7clite::kRetModeEntries[0], size_t(8));

    for (uint32_t r : rs7clite::kRetModeEntries) {
        CHECK(r != rs7clite::kRetTunerCancel);
    }

    // Session / Guitarcade / Lessons / Technique hub keep their protection.
    const uint32_t kept[] = {0x60726C, 0x60C7D5, 0x613928, 0x6DBD09, 0x6DBD63, 0x6F44BE, 0x6F44FA, 0x73D904};

    for (uint32_t r : kept) {
        CHECK(rs7clite::ModeEntry(r) != nullptr);
        CHECK_EQ(RebuildTarget(r, 0, 6, 7), 6u);
    }
}

TEST(escrebuild_no_rebuild_while_a_tuner_holds_the_guitar) {
    using rs7clite::RebuildTarget;

    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6, true), 0u);
    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6, false), 7u);
    CHECK_EQ(RebuildTarget(0x6F44BE, 0, 6, 7, true), 0u);
    CHECK_EQ(RebuildTarget(0x6F44BE, 0, 6, 7, false), 6u);
}

TEST(escrebuild_tuners_holding_reads_the_manager_slots) {
    alignas(4) uint8_t mgr[0x24] = {}, t0[0x40] = {}, t2[0x40] = {};
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    uintptr_t g = 0;

    CHECK_EQ(rs7clite::TunersHolding(0, &g), 0u);
    CHECK_EQ(rs7clite::TunersHolding(m, &g), 0u);  // no player tuner
    uint8_t* p0 = t0;

    std::memcpy(mgr + rs7clite::kTunerMgrSlots, &p0, 4);
    CHECK_EQ(rs7clite::TunersHolding(m, &g), 0u);  // a tuner without a guitar
    const uintptr_t guitar = 0x1A567F30;

    std::memcpy(t2 + rs7clite::kTunerGuitar, &guitar, 4);
    uint8_t* p2 = t2;

    std::memcpy(mgr + rs7clite::kTunerMgrSlots + 2 * 4, &p2, 4);
    CHECK_EQ(rs7clite::TunersHolding(m, &g), 1u);
    CHECK_EQ(g, guitar);
    // An unreadable tuner pointer counts as held (no rebuild is the safe side).
    const uintptr_t bad = 0x10;

    std::memcpy(mgr + rs7clite::kTunerMgrSlots, &bad, 4);
    CHECK(rs7clite::TunersHolding(m, nullptr) >= 1u);
}

TEST(escrebuild_guitar_gone_drops_every_per_guitar_state) {
    const uintptr_t was = g_rs7_asset_on;

    g_rs7_asset_on = 1;
    Rs7Alloc_ResetForTests();
    alignas(4) static uint8_t guitar[0x50], other[0x50], strings[0x40];

    std::memset(guitar, 0, sizeof guitar);
    std::memset(other, 0, sizeof other);
    void* gs = strings;

    std::memcpy(guitar + 0x0C, &gs, 4);
    Rs7Asset_HeadFrame(guitar, false);
    Rs7Asset_HeadFrame(other, false);
    CHECK_EQ(Rs7Asset_LiveHeads(), 2);
    Rs7S7_SetGuitarMode(guitar, true);
    Rs7S7Stats before{}, after{};

    Rs7S7_Stats(&before);
    Rs7_GuitarGone(guitar);
    Rs7S7_Stats(&after);
    CHECK_EQ(Rs7Asset_LiveHeads(), 1);  // the other guitar keeps its slot
    CHECK_EQ(after.sevenGuitars, before.sevenGuitars - 1);
    Rs7_GuitarGone(guitar);  // idempotent
    Rs7_GuitarGone(nullptr);
    CHECK_EQ(Rs7Asset_LiveHeads(), 1);
    Rs7Asset_ForgetGuitar(other);
    CHECK_EQ(Rs7Asset_LiveHeads(), 0);
    CHECK_EQ(Rs7Alloc_Live(kAkHeadSlots), 0);
    Rs7Alloc_ResetForTests();
    g_rs7_asset_on = was;
}
