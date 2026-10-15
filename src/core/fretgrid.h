#pragma once
#include "hooks/midhook.h"
#include <cstdint>

// Native capacity-8 Rnd_FretGrid (issue #18 stage 5 area 1, docs/ARCHITECTURE.md
// section 1), a member of the native 7-string unit (no ini switch since 1.0).
//
// Layout (every FretGrid of the process, guitar and bass alike):
//   cells   +4          26 rows of 4 + 8 * 4Ch = 264h (stock 6 * 4Ch = 1CCh); row byte 0 = row valid, cell s at
//                       row + 4 + s * 4Ch: +0 valid, +4 three local vec3, +28h three world vec3
//   wires   +72A0h      26 rows of A0h + 8 * 30h = 220h (stock +2EC0h, 1C0h); the error records of strings 6/7
//                       start empty
//   tail    +5C40h..    unchanged (fretbars, spans, flags); owner size A9E0h (stock 72A0h), allocation and free
// The edits are in-place byte patches (sites FGR<va>, tools/gen_fretgrid.py); no function is copied. The active
// string counts (InstrumentInfo +0Bh) are untouched, so strings 6/7 are stored but not published.
// FGRB (the build's "complete" exit 0x58FA25) fills the local points of cells 6/7 of a 6-string guitar:
//   cell6 = 2 * cell0 - cell1, cell7 = 3 * cell0 - 2 * cell1 (the world points follow once ApplyOffset
// runs over eight strings), valid only when cells 0 and 1 are valid.

extern "C" {
// The native FretGrid is on (set by startup before any site is patched; a deferred write failure clears it).
__declspec(dllexport) extern uintptr_t g_rs7_native_fretgrid;
// The row stride of a FretGrid cell row: 1CCh stock, 264h native. Read by the R1 / V2 stubs and the C++ readers.
__declspec(dllexport) extern uintptr_t g_rs7_fg_row;
// FGHK (mid-hook at the Rnd_Guitar per-frame update 0x592AC0, [esp+4] = guitar): see rs7fg::OnGuitarUpdate.
__declspec(dllexport) void __cdecl Rs7_Mid_FGHK(Rs7Mid::Context& c);
// FGCT (mid-hook after the FretGrid ctor in the guitar ctor 0x59267B, eax = grid): registers native grids.
__declspec(dllexport) void __cdecl Rs7_Mid_FGCT(Rs7Mid::Context& c);
// FGRB (cdecl, called from the stub): grid, guitar.
__declspec(dllexport) void __cdecl Rs7Fg_BuildExtra(uint8_t* grid, uint8_t* guitar);
}

namespace rs7fg {
constexpr uint32_t kRowStock = 0x1CC, kRowNative = 0x264;
constexpr uint32_t kWireRowStock = 0x1C0, kWireRowNative = 0x220;
constexpr uint32_t kWireBaseStock = 0x2EC0, kWireBaseNative = 0x72A0;
constexpr uint32_t kSizeStock = 0x72A0, kSizeNative = 0xA9E0;
constexpr uint32_t kCellsOff = 4, kCell = 0x4C, kFrets = 26, kCapacity = 8, kStock = 6;
constexpr uint32_t kCellLocal = 4, kCellWorld = 0x28;

// Sets the switch and the stride variable (startup, and tests).
void Configure(bool native);
// Startup: the switch is on but the layout is not yet in the code: the stride variable stays stock until the
// deferred sites are written (OnGuitarUpdate).
void ConfigureDeferred(bool native);
// Startup: the resolved FGR sites are written by this function (restores them and returns false on failure).
using ApplyFn = bool (*)();
// Until it is called, OnGuitarUpdate does nothing (FGHK can run before startup finishes the batch).
void SetDeferredApply(ApplyFn apply);
// True while native_fretgrid is on and the deferred layout is still to be written (not failed).
bool LayoutPending();
// The migration, pure (tests): a native grid built from a stock-size one. `fresh` = a constructed native grid.
// Header dword, cell rows, wire rows and the tail move to their native offsets; the cells / wire records of
// strings 6 and 7 stay as constructed.
void MigrateGrid(const uint8_t* old, uint8_t* fresh);
// Native-grid registry (pointer set; stock-size blocks cannot be sized, the game's allocator is not the CRT's):
// a grid is native iff it was built after the layout was written (OnGridCreated, FGCT) or already migrated.
// Fixed-capacity (kRegistryCapacity, no allocation, oldest-generation eviction; see fretgrid.cpp for why there is
// no per-destruction removal). Nothing here can throw.
constexpr unsigned kRegistryCapacity = 256;
void ResetRegistry() noexcept;
bool MarkNative(const uint8_t* grid) noexcept;      // false: not registered (fail-safe: migration is off)
bool NeedsMigration(const uint8_t* grid) noexcept;  // non-null and not registered (false after a failure)
unsigned RegistrySize() noexcept;
bool RegistryFailed() noexcept;
void DisableMigration() noexcept;
void InjectRegistryFailure(bool on) noexcept;  // tests only
void OnGridCreated(uint8_t* grid) noexcept;    // FGCT: registers once the layout is written
// Every call (game thread, guitar update entry, before any FretGrid code of that frame): the first writes the
// deferred FGR sites and switches the stride variable; each guitar whose FretGrid is not registered native (built
// before the layout existed) gets a native one, once.
void OnGuitarUpdate(uint8_t* guitar);

inline uint32_t RowStride() {
    return static_cast<uint32_t>(g_rs7_fg_row);
}

// Pure: the native-layout cell s of fret row f (grid = the FretGrid base).
inline uint8_t* Cell(uint8_t* grid, uint32_t f, uint32_t s) {
    return grid + kCellsOff + f * kRowNative + 4 + s * kCell;
}

// Pure: cells 6/7 of the first `frets` rows from cells 0/1 (native layout). Returns the rows filled.
int FillExtraCells(uint8_t* grid, uint32_t frets);
}  // namespace rs7fg
