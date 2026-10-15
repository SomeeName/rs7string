#include "core/fretgrid.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace rs7fg;

namespace {
void PutF(uint8_t* p, uint32_t off, float v) {
    std::memcpy(p + off, &v, 4);
}

float GetF(const uint8_t* p, uint32_t off) {
    float v;

    std::memcpy(&v, p + off, 4);

    return v;
}
}  // namespace

TEST(fretgrid_layout_arithmetic) {
    CHECK_EQ(kRowNative, 4u + 8u * kCell);
    CHECK_EQ(kRowStock, 4u + 6u * kCell);
    CHECK_EQ(kCellsOff + kFrets * kRowNative, 0x3E2Cu);  // cells end below the retained tail (+5C40h)
    CHECK(kCellsOff + kFrets * kRowNative <= 0x5C40u);
    CHECK_EQ(kWireBaseNative, kSizeStock);  // wires move behind the stock end
    CHECK_EQ(kWireBaseNative + kFrets * kWireRowNative, kSizeNative);
    CHECK_EQ(kWireRowNative, 0xA0u + 8u * 0x30u);
    CHECK_EQ(kWireRowStock, 0xA0u + 6u * 0x30u);
    CHECK_EQ(kRowStock - kRowNative, uint32_t(-0x98));  // ApplyOffset displacement shift
}

TEST(fretgrid_configure_sets_switch_and_stride) {
    const uintptr_t sw = g_rs7_native_fretgrid, row = g_rs7_fg_row;

    Configure(true);
    CHECK_EQ(g_rs7_native_fretgrid, uintptr_t(1));
    CHECK_EQ(RowStride(), kRowNative);
    Configure(false);
    CHECK_EQ(g_rs7_native_fretgrid, uintptr_t(0));
    CHECK_EQ(RowStride(), kRowStock);
    g_rs7_native_fretgrid = sw;
    g_rs7_fg_row = row;
}

TEST(fretgrid_extra_cells_extrapolate_local_points) {
    std::vector<uint8_t> g(kSizeNative, 0);
    const uint32_t frets = 3;

    for (uint32_t f = 0; f < frets; ++f) {
        for (uint32_t s = 0; s < 2; ++s) {
            uint8_t* c = Cell(g.data(), f, s);

            c[0] = 1;

            for (uint32_t i = 0; i < 9; ++i) {
                PutF(c, kCellLocal + 4 * i, float(10 * f + 4 * s + i));  // linear in s
            }
        }
    }

    CHECK_EQ(FillExtraCells(g.data(), frets), int(frets));

    for (uint32_t f = 0; f < frets; ++f) {
        for (uint32_t i = 0; i < 9; ++i) {
            const float c0 = float(10 * f + i);

            CHECK_EQ(GetF(Cell(g.data(), f, 6), kCellLocal + 4 * i), c0 - 4.0f);  // 2*c0 - c1 with c1 = c0 + 4
            CHECK_EQ(GetF(Cell(g.data(), f, 7), kCellLocal + 4 * i), c0 - 8.0f);  // 3*c0 - 2*c1
        }

        CHECK_EQ(Cell(g.data(), f, 6)[0], uint8_t(1));
        CHECK_EQ(Cell(g.data(), f, 7)[0], uint8_t(1));

        for (uint32_t i = 0; i < 9; ++i) {
            CHECK_EQ(GetF(Cell(g.data(), f, 6), kCellWorld + 4 * i), 0.0f);  // world: ApplyOffset
        }
    }

    CHECK_EQ(Cell(g.data(), 3, 6)[0], uint8_t(0));  // rows past the fret count stay empty
}

TEST(fretgrid_extra_cells_need_both_source_cells) {
    std::vector<uint8_t> g(kSizeNative, 0);

    Cell(g.data(), 0, 0)[0] = 1;  // cell 1 missing
    Cell(g.data(), 1, 1)[0] = 1;  // cell 0 missing
    CHECK_EQ(FillExtraCells(g.data(), 2), 0);
    CHECK_EQ(Cell(g.data(), 0, 6)[0], uint8_t(0));
    CHECK_EQ(Cell(g.data(), 1, 7)[0], uint8_t(0));
    CHECK_EQ(FillExtraCells(g.data(), 99), 0);  // clamped to 26 rows, still nothing valid
}

TEST(fretgrid_build_extra_is_off_by_default_and_only_for_six_string_infos) {
    std::vector<uint8_t> g(kSizeNative, 0);
    uint8_t info[0x40] = {};

    info[0xB] = 6;
    info[0xC] = 2;
    uint8_t* inf = info;
    uint8_t guitar[0x40] = {};

    std::memcpy(guitar + 0x24, &inf, 4);
    Cell(g.data(), 0, 0)[0] = 1;
    Cell(g.data(), 0, 1)[0] = 1;
    const uintptr_t sw = g_rs7_native_fretgrid;

    g_rs7_native_fretgrid = 0;
    Rs7Fg_BuildExtra(g.data(), guitar);
    CHECK_EQ(Cell(g.data(), 0, 6)[0], uint8_t(0));  // switch off: nothing
    g_rs7_native_fretgrid = 1;
    info[0xB] = 4;  // bass
    Rs7Fg_BuildExtra(g.data(), guitar);
    CHECK_EQ(Cell(g.data(), 0, 6)[0], uint8_t(0));
    info[0xB] = 6;
    Rs7Fg_BuildExtra(g.data(), guitar);
    CHECK_EQ(Cell(g.data(), 0, 6)[0], uint8_t(1));
    Rs7Fg_BuildExtra(g.data(), nullptr);  // no crash
    g_rs7_native_fretgrid = sw;
}

TEST(fretgrid_deferred_configure_keeps_the_stock_stride) {
    const uintptr_t sw = g_rs7_native_fretgrid, row = g_rs7_fg_row;

    rs7fg::ConfigureDeferred(true);
    CHECK_EQ(g_rs7_native_fretgrid, uintptr_t(1));
    CHECK_EQ(rs7fg::RowStride(), rs7fg::kRowStock);  // the layout is not in the code yet
    g_rs7_native_fretgrid = sw;
    g_rs7_fg_row = row;
}

TEST(fretgrid_migrate_moves_every_region_to_its_native_offset) {
    using namespace rs7fg;
    std::vector<uint8_t> old(kSizeStock, 0), fresh(kSizeNative, 0xEE);

    for (size_t i = 0; i < old.size(); ++i) {
        old[i] = uint8_t(i * 7 + (i >> 8));
    }

    MigrateGrid(old.data(), fresh.data());
    CHECK(std::memcmp(fresh.data(), old.data(), 4) == 0);

    for (uint32_t r = 0; r < kFrets; ++r) {
        CHECK(std::memcmp(fresh.data() + kCellsOff + r * kRowNative, old.data() + kCellsOff + r * kRowStock,
                          kRowStock) == 0);
        CHECK(std::memcmp(fresh.data() + kWireBaseNative + r * kWireRowNative,
                          old.data() + kWireBaseStock + r * kWireRowStock, kWireRowStock) == 0);
        CHECK_EQ(fresh[kCellsOff + r * kRowNative + kRowStock], uint8_t(0xEE));  // cells 6/7 untouched
        CHECK_EQ(fresh[kWireBaseNative + r * kWireRowNative + kWireRowStock], uint8_t(0xEE));
    }

    CHECK(std::memcmp(fresh.data() + 0x5C40, old.data() + 0x5C40, kSizeStock - 0x5C40) == 0);
}

TEST(fretgrid_registry_migrates_every_preexisting_grid_once) {
    using namespace rs7fg;

    ResetRegistry();
    uint8_t oldA[8], oldB[8], created[8];

    CHECK(NeedsMigration(oldA));
    CHECK(NeedsMigration(oldB));  // two guitars that predate the layout
    CHECK(!NeedsMigration(nullptr));
    MarkNative(created);  // built after the layout (FGCT)
    CHECK(!NeedsMigration(created));
    MarkNative(oldA);  // migrated
    CHECK(!NeedsMigration(oldA));
    CHECK(NeedsMigration(oldB));
    MarkNative(oldB);
    CHECK(!NeedsMigration(oldB));
    OnGridCreated(oldA);  // before the layout is written: ignored
    ResetRegistry();
}

namespace {
int g_applyCalls = 0;

bool FakeApply() {
    ++g_applyCalls;

    return true;
}
}  // namespace

// CONFIRMED in the game (tests/full-test/A_try3_stuck.log): FGHK runs on the game thread while startup is still
// patching; an update before SetDeferredApply used to switch native_fretgrid off silently (native_publish then
// waited for the layout forever). It must wait for the hand-over instead.
TEST(fretgrid_update_before_the_deferred_handover_waits) {
    using namespace rs7fg;
    const uintptr_t sw = g_rs7_native_fretgrid, row = g_rs7_fg_row;

    ConfigureDeferred(true);
    ResetRegistry();
    uint8_t grid[16] = {};
    uint8_t guitar[0x40] = {};
    uint8_t* gp = grid;

    std::memcpy(guitar + 0x10, &gp, 4);
    MarkNative(grid);  // no migration (GameNew is the game's allocator)
    g_applyCalls = 0;
    OnGuitarUpdate(guitar);  // startup has not handed over the sites yet (no other test does)
    CHECK_EQ(g_rs7_native_fretgrid, uintptr_t(1));
    CHECK_EQ(RowStride(), kRowStock);
    CHECK(LayoutPending());
    CHECK_EQ(g_applyCalls, 0);
    SetDeferredApply(&FakeApply);
    MarkNative(grid);
    OnGuitarUpdate(guitar);
    CHECK_EQ(g_applyCalls, 1);
    CHECK_EQ(RowStride(), kRowNative);
    CHECK(!LayoutPending());
    OnGuitarUpdate(guitar);
    CHECK_EQ(g_applyCalls, 1);
    ResetRegistry();
    g_rs7_native_fretgrid = sw;
    g_rs7_fg_row = row;
}

// Review #17: the registry is bounded (it used to grow with every grid ever created) and keeps recent grids.
TEST(fretgrid_registry_is_bounded_and_evicts_oldest_first) {
    using namespace rs7fg;

    ResetRegistry();
    static_assert(noexcept(MarkNative(nullptr)) && noexcept(NeedsMigration(nullptr)), "registry must not throw");
    std::vector<uint8_t> mem(kRegistryCapacity + 40 + 1);

    for (size_t i = 0; i < mem.size(); ++i) {
        MarkNative(&mem[i]);
    }

    CHECK_EQ(RegistrySize(), kRegistryCapacity);   // bounded
    CHECK(NeedsMigration(&mem[0]));                // the oldest were evicted
    CHECK(!NeedsMigration(&mem[mem.size() - 1]));  // the newest are native
    CHECK(!NeedsMigration(&mem[40 + 1]));          // exactly the last kRegistryCapacity remain
    MarkNative(&mem[mem.size() - 1]);              // re-marking a recycled address: no growth
    CHECK_EQ(RegistrySize(), kRegistryCapacity);
    ResetRegistry();
    CHECK_EQ(RegistrySize(), 0u);
}

// Review #7: a failed registration must not escape and must never let a native grid look stock-size.
TEST(fretgrid_failed_registration_disables_migration_instead_of_throwing) {
    using namespace rs7fg;
    const uintptr_t sw = g_rs7_native_fretgrid, row = g_rs7_fg_row;

    ConfigureDeferred(true);
    uint8_t grid[16] = {}, other[16] = {}, guitar[0x40] = {};
    uint8_t* gp = grid;

    std::memcpy(guitar + 0x10, &gp, 4);
    SetDeferredApply(&FakeApply);
    MarkNative(grid);
    OnGuitarUpdate(guitar);        // layout "applied": OnGridCreated is live now
    CHECK(NeedsMigration(other));  // normal: unknown grid = stock-size
    InjectRegistryFailure(true);
    bool escaped = false;

    try {
        OnGridCreated(other);
    } catch (...) {
        escaped = true;
    }

    InjectRegistryFailure(false);
    CHECK(!escaped);
    CHECK(RegistryFailed());
    CHECK(!NeedsMigration(other));  // fail-safe: the unregistered native grid is not "migrated"
    CHECK(!NeedsMigration(grid));
    ResetRegistry();
    g_rs7_native_fretgrid = sw;
    g_rs7_fg_row = row;
}
