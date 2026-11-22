#include "features/allocstats/allocstats.h"
#include "features/clite/clite.h"
#include "features/headstock/headstock.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "test.h"
#include "testutil.h"
#include <cstdint>
#include <cstring>

// fix/escrebuild: Esc in a 7-string pre-song tuner crashed (hs33 .mdmp: the live tuner applied transforms to the
// guitar a mode-entry rebuild had just freed).

TEST(escrebuild_tuner_cancel_is_not_a_mode_entry) {
    using rsxrclite::RebuildTarget;

    CHECK(rsxrclite::ModeEntry(rsxrclite::kRetTunerCancel) == nullptr);
    CHECK_EQ(RebuildTarget(rsxrclite::kRetTunerCancel, 0, 6, 7), 0u);
    CHECK_EQ(RebuildTarget(rsxrclite::kRetTunerCancel, 0, 6, 8), 0u);
    CHECK_EQ(sizeof rsxrclite::kRetModeEntries / sizeof rsxrclite::kRetModeEntries[0], size_t(8));

    for (const auto& entry : rsxrclite::kRetModeEntries) {
        const uint32_t r = entry.ret;

        CHECK(r != rsxrclite::kRetTunerCancel);
    }

    // Session / Guitarcade / Lessons / Technique hub keep their protection.
    const uint32_t kept[] = {0x60726C, 0x60C7D5, 0x613928, 0x6DBD09, 0x6DBD63, 0x6F44BE, 0x6F44FA, 0x73D904};

    for (uint32_t r : kept) {
        CHECK(rsxrclite::ModeEntry(r) != nullptr);
        CHECK_EQ(RebuildTarget(r, 0, 6, 7), 6u);
    }
}

TEST(escrebuild_no_rebuild_while_a_tuner_holds_the_guitar) {
    using rsxrclite::RebuildTarget;

    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6, true), 0u);
    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6, false), 7u);
    CHECK_EQ(RebuildTarget(0x6F44BE, 0, 6, 7, true), 0u);
    CHECK_EQ(RebuildTarget(0x6F44BE, 0, 6, 7, false), 6u);
}

TEST(escrebuild_tuners_holding_reads_the_manager_slots) {
    alignas(4) uint8_t mgr[0x24] = {}, t0[0x40] = {}, t2[0x40] = {};
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    uintptr_t g = 0;

    CHECK_EQ(rsxrclite::TunersHolding(0, &g), 0u);
    CHECK_EQ(rsxrclite::TunersHolding(m, &g), 0u);  // no player tuner
    uint8_t* p0 = t0;

    std::memcpy(mgr + rsxrclite::kTunerMgrSlots, &p0, 4);
    CHECK_EQ(rsxrclite::TunersHolding(m, &g), 0u);  // a tuner without a guitar
    const uintptr_t guitar = 0x1A567F30;

    std::memcpy(t2 + rsxrclite::kTunerGuitar, &guitar, 4);
    uint8_t* p2 = t2;

    std::memcpy(mgr + rsxrclite::kTunerMgrSlots + 2 * 4, &p2, 4);
    CHECK_EQ(rsxrclite::TunersHolding(m, &g), 1u);
    CHECK_EQ(g, guitar);
    // An unreadable tuner pointer counts as held (no rebuild is the safe side).
    const uintptr_t bad = 0x10;

    std::memcpy(mgr + rsxrclite::kTunerMgrSlots, &bad, 4);
    CHECK(rsxrclite::TunersHolding(m, nullptr) >= 1u);
}

TEST(escrebuild_guitar_gone_drops_every_per_guitar_state) {
    rsxrtest::Restore restore_g_rsxr_asset_on(g_rsxr_asset_on);

    g_rsxr_asset_on = 1;
    RsxrAlloc_ResetForTests();
    alignas(4) static uint8_t guitar[0x50], other[0x50], strings[0x40];

    std::memset(guitar, 0, sizeof guitar);
    std::memset(other, 0, sizeof other);
    void* gs = strings;

    std::memcpy(guitar + 0x0C, &gs, 4);
    RsxrAsset_HeadFrame(guitar, false);
    RsxrAsset_HeadFrame(other, false);
    CHECK_EQ(RsxrAsset_LiveHeads(), 2);
    RsxrS7_SetGuitarMode(guitar, true);
    RsxrS7Stats before{}, after{};

    RsxrS7_Stats(&before);
    Rsxr_GuitarGone(guitar);
    RsxrS7_Stats(&after);
    CHECK_EQ(RsxrAsset_LiveHeads(), 1);  // the other guitar keeps its slot
    CHECK_EQ(after.sevenGuitars, before.sevenGuitars - 1);
    Rsxr_GuitarGone(guitar);  // idempotent
    Rsxr_GuitarGone(nullptr);
    CHECK_EQ(RsxrAsset_LiveHeads(), 1);
    RsxrAsset_ForgetGuitar(other);
    CHECK_EQ(RsxrAsset_LiveHeads(), 0);
    CHECK_EQ(RsxrAlloc_Live(kAkHeadSlots), 0);
    RsxrAlloc_ResetForTests();
}
