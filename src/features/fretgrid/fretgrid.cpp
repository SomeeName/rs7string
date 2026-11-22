// The native FretGrid's logic (fretgrid.h): the native-grid registry, the stock -> native grid migration and the
// extra cells 6/7. Values in, values out; the game glue is hooks.cpp.
#include "features/fretgrid/fretgrid.h"
#include "model/instrument.h"
#include <cstring>

namespace rsxrfg {
namespace {
// Native-layout grids (game thread only). Registration cannot allocate or throw into FGCT. Creation age cannot
// prove a grid dead: never evict an entry. Exhaustion disables migration, just like any other failed registration,
// so a native block can never be copied or sized-freed as stock. Recycled addresses reuse their existing entry.
struct RegEntry {
    uintptr_t addr;
};

RegEntry g_reg[kRegistryCapacity] = {};
bool g_regFault = false, g_regInject = false;
}  // namespace

void ResetRegistry() noexcept {
    for (auto& e : g_reg) {
        e = RegEntry{};
    }

    g_regFault = g_regInject = false;
}

bool MarkNative(const uint8_t* grid) noexcept {
    if (!grid) {
        return true;
    }

    if (g_regInject) {
        g_regFault = true;
        return false;
    }  // test hook: a failed registration

    const uintptr_t a = reinterpret_cast<uintptr_t>(grid);
    RegEntry* slot = nullptr;

    for (auto& e : g_reg) {
        if (e.addr == a) {
            slot = &e;
            break;
        }

        if (!slot && !e.addr) {
            slot = &e;
        }
    }

    if (!slot) {
        g_regFault = true;
        return false;
    }

    *slot = RegEntry{a};

    return true;
}

bool IsNative(const uint8_t* grid) noexcept {
    if (!grid) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(grid);

    for (const auto& e : g_reg) {
        if (e.addr == a) {
            return true;
        }
    }

    return false;
}

bool NeedsMigration(const uint8_t* grid) noexcept {
    // After a failed registration an unknown grid may be native or stock: neither migrate nor update it.
    return grid && !g_regFault && !IsNative(grid);
}

unsigned RegistrySize() noexcept {
    unsigned n = 0;

    for (const auto& e : g_reg) {
        n += e.addr != 0;
    }

    return n;
}

bool RegistryFailed() noexcept {
    return g_regFault;
}

void DisableMigration() noexcept {
    g_regFault = true;
}

void InjectRegistryFailure(bool on) noexcept {
    g_regInject = on;
}

void MigrateGrid(const uint8_t* old, uint8_t* fresh) {
    std::memcpy(fresh, old, 4);  // header (owner)

    for (uint32_t r = 0; r < kFrets; ++r) {
        std::memcpy(fresh + kCellsOff + r * kRowNative, old + kCellsOff + r * kRowStock,
                    kRowStock);  // flag + cells 0-5
        std::memcpy(fresh + kWireBaseNative + r * kWireRowNative, old + kWireBaseStock + r * kWireRowStock,
                    kWireRowStock);
    }

    // the tail (fret bars, spans, flags) sits at the same offsets in both layouts
    constexpr uint32_t tail = rsxr::kGridTail;
    static_assert(tail == kWireBaseStock + kFrets * kWireRowStock, "the tail follows the stock wire rows");

    std::memcpy(fresh + tail, old + tail, kSizeStock - tail);
}

int FillExtraCells(uint8_t* grid, uint32_t frets) {
    if (frets > kFrets) {
        frets = kFrets;
    }
    int done = 0;

    for (uint32_t f = 0; f < frets; ++f) {
        const uint8_t* c0 = Cell(grid, f, 0);
        const uint8_t* c1 = Cell(grid, f, 1);

        if (!c0[0] || !c1[0]) {
            Cell(grid, f, 6)[0] = Cell(grid, f, 7)[0] = 0;
            continue;
        }

        for (uint32_t s = 6; s < 8; ++s) {
            uint8_t* c = Cell(grid, f, s);

            for (uint32_t i = 0; i < 9; ++i) {
                float a, b;

                std::memcpy(&a, c0 + kCellLocal + 4 * i, 4);
                std::memcpy(&b, c1 + kCellLocal + 4 * i, 4);
                const float v = s == 6 ? 2.0f * a - b : 3.0f * a - 2.0f * b;

                std::memcpy(c + kCellLocal + 4 * i, &v, 4);
            }

            c[0] = 1;
        }

        ++done;
    }

    return done;
}
}  // namespace rsxrfg
