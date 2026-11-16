#include "features/fretgrid/fretgrid.h"
#include "model/instrument.h"
#include "test.h"
#include <windows.h>
#include <cstring>

namespace {
uint8_t g_fresh[rs7fg::kSizeNative], g_stock[rs7fg::kSizeStock];
bool g_ctorFault = false, g_allocFails = false;
int g_allocations = 0;

void* Allocate(uint32_t size) {
    CHECK_EQ(size, rs7fg::kSizeNative);
    ++g_allocations;

    return g_allocFails ? nullptr : g_fresh;
}

void* Construct(void* grid) {
    if (g_ctorFault) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }

    return grid;
}

void Free(void*, uint32_t size) {
    CHECK_EQ(size, rs7fg::kSizeStock);
}

rs7fg::ApplyResult Apply(bool) {
    return rs7fg::ApplyResult::Applied;
}

struct Migration {
    uintptr_t sw = g_rs7_native_fretgrid, row = g_rs7_fg_row;
    uint8_t guitar[0x40] = {};

    Migration() {
        uint8_t* grid = g_stock;

        std::memset(g_stock, 0, sizeof g_stock);

        std::memcpy(guitar + rs7::kGuitarGrid, &grid, sizeof grid);
        rs7fg::ConfigureDeferred(true);
        rs7fg::SetDeferredApply(&Apply);
        rs7fg::SetGridAllocatorForTests(&Allocate, &Construct, &Free);
        g_ctorFault = g_allocFails = false;
        g_allocations = 0;
    }

    ~Migration() {
        rs7fg::SetGridAllocatorForTests(nullptr, nullptr, nullptr);
        rs7fg::SetDeferredApply(nullptr);
        g_rs7_native_fretgrid = sw;
        g_rs7_fg_row = row;
    }

    void Update(bool skipped) {
        uint32_t stack[] = {0x12345678, uint32_t(uintptr_t(guitar))};
        Rs7Mid::Context c = {};

        c.esp = uint32_t(uintptr_t(stack));
        c.jump = 0x87654321;
        Rs7_Mid_FGHK(c);
        CHECK_EQ(c.jump, skipped ? 0x12345678u : 0x87654321u);
        CHECK_EQ(c.esp, uint32_t(uintptr_t(stack)) + (skipped ? 8u : 0u));
    }

    uint8_t* Grid() const {
        uint8_t* grid;

        std::memcpy(&grid, guitar + rs7::kGuitarGrid, sizeof grid);

        return grid;
    }
};
}  // namespace

TEST(fretgrid_ctor_fault_skips_update_until_migration_succeeds) {
    Migration m;

    g_ctorFault = true;
    m.Update(true);
    CHECK(m.Grid() == g_stock);
    CHECK_EQ(rs7fg::RowStride(), rs7fg::kRowNative);
    g_ctorFault = false;
    m.Update(false);
    CHECK(m.Grid() == g_fresh);
    CHECK(!rs7fg::NeedsMigration(m.Grid()));
}

TEST(fretgrid_migration_fault_skips_update_until_grid_is_readable) {
    Migration m;
    uint8_t* unreadable = static_cast<uint8_t*>(rs7test::Must(
        VirtualAlloc(nullptr, rs7fg::kSizeStock, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS), "VirtualAlloc"));

    std::memcpy(m.guitar + rs7::kGuitarGrid, &unreadable, sizeof unreadable);
    m.Update(true);  // MigrateGrid faults before the slot is replaced
    CHECK(m.Grid() == unreadable);
    uint8_t* stock = g_stock;

    std::memcpy(m.guitar + rs7::kGuitarGrid, &stock, sizeof stock);
    VirtualFree(unreadable, 0, MEM_RELEASE);
    m.Update(false);
    CHECK(m.Grid() == g_fresh);
}

TEST(fretgrid_ctor_faults_keep_stock_grid_updates_skipped_after_giving_up) {
    Migration m;

    g_ctorFault = true;

    for (int i = 0; i < 3; ++i) {
        m.Update(true);
        CHECK(m.Grid() == g_stock);
    }

    g_ctorFault = false;
    m.Update(true);
    m.Update(true);
    CHECK(m.Grid() == g_stock);
    CHECK_EQ(g_allocations, 3);  // no further attempts, and never a native update on the old grid
}

TEST(fretgrid_allocation_failure_skips_update_and_can_retry) {
    Migration m;

    g_allocFails = true;
    m.Update(true);
    CHECK(m.Grid() == g_stock);
    g_allocFails = false;
    m.Update(false);
    CHECK(m.Grid() == g_fresh);
}

TEST(fretgrid_registry_exhaustion_skips_stock_grids_waiting_for_migration) {
    for (bool retry : {false, true}) {
        Migration m;
        uint8_t firstGuitar[0x40] = {}, native[rs7fg::kRegistryCapacity + 1] = {};

        CHECK(rs7fg::OnGuitarUpdate(firstGuitar));  // switch layouts before this stock grid's first update
        g_allocFails = true;

        if (retry) {
            m.Update(true);
        }

        for (auto& grid : native) {
            rs7fg::OnGridCreated(&grid);
        }

        CHECK(rs7fg::RegistryFailed());
        const int allocations = g_allocations;

        g_allocFails = false;
        m.Update(true);
        CHECK(m.Grid() == g_stock);
        CHECK_EQ(g_allocations, allocations);  // an unknown layout must not be copied or sized-freed as stock
        uint8_t* known = &native[0];

        std::memcpy(m.guitar + rs7::kGuitarGrid, &known, sizeof known);
        m.Update(false);  // registered native grids remain usable
        uint8_t* unknown = &native[rs7fg::kRegistryCapacity];

        std::memcpy(m.guitar + rs7::kGuitarGrid, &unknown, sizeof unknown);
        m.Update(true);  // an unregistered native grid is also skipped, never migrated as stock
    }
}
