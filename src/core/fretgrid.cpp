#include "core/fretgrid.h"
#include "core/colournative.h"
#include "log.h"
#include <windows.h>
#include <cstring>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_native_fretgrid = 0;
__declspec(dllexport) uintptr_t g_rs7_fg_row = 0x1CC;
}

namespace rs7fg {
void Configure(bool native) {
    g_rs7_native_fretgrid = native ? 1 : 0;
    g_rs7_fg_row = native ? kRowNative : kRowStock;
}

void ConfigureDeferred(bool native) {
    g_rs7_native_fretgrid = native ? 1 : 0;
    g_rs7_fg_row = kRowStock;
}

namespace {
ApplyFn g_apply = nullptr;
bool g_applied = false, g_failed = false;
// Set (last, with a barrier) by SetDeferredApply once startup has collected the deferred sites. FGHK is written
// with the main batch on the startup thread, so the game thread can reach OnGuitarUpdate before that: until
// then it must wait (CONFIRMED in the game, tests/full-test A_try3_stuck.log: the early update saw g_apply ==
// nullptr, switched native_fretgrid off silently and left native_publish / native_tuner waiting forever).
volatile long g_ready = 0;
int g_migrated = 0;

// Native-layout grids (game thread only). A fixed array: registration cannot allocate, so it cannot throw into
// the FGCT hook (review #7), and it cannot grow without bound (review #17). The game has no FretGrid
// destruction hook of ours (the free happens inside the guitar release, 0x592770 -> sized free), and dropping a
// LIVE grid from the registry would make it look stock-size and get "migrated" (copy + sized free of a block
// 0xA9E0 bytes large as 0x72A0): so entries are only ever evicted oldest-first, by generation, when more than
// kRegistryCapacity grids were created since. Live guitars number a handful; the capacity is far beyond that, so
// only long-dead grids are evicted. A re-created grid at a recycled address just refreshes its entry.
struct RegEntry {
    uintptr_t addr;
    uint32_t gen;
};

RegEntry g_reg[kRegistryCapacity] = {};
uint32_t g_regGen = 0;
bool g_regFault = false, g_regInject = false;

// Game allocator: operator new 0x8F9D40 (cdecl size, flags), constructor 0x58F090 (stdcall this, returns this),
// sized free through the allocator object 0x91FFA0 (vtable +8: ecx = allocator, stack ptr, 3, size; ret 0Ch).
void* GameNew(uint32_t nbytes) {
    void* r;

    __asm {
        push 0
        push nbytes
        mov eax, 0x8F9D40
        call eax
        add esp, 8
        mov r, eax
    }

    return r;
}

void* GameCtor(void* mem) {
    void* r;

    __asm {
        push mem
        mov eax, 0x58F090
        call eax
        mov r, eax
    }

    return r;
}

void GameFree(void* p, uint32_t nbytes) {
    __asm {
        mov eax, 0x91FFA0
        call eax
        mov ecx, dword ptr [eax]
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [eax + 8]
        push nbytes
        push 3
        push p
        call edx
    }
}

}  // namespace

void ResetRegistry() noexcept {
    for (auto& e : g_reg) {
        e = RegEntry{};
    }

    g_regGen = 0;
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

    if (!slot) {  // full: evict the oldest generation
        slot = &g_reg[0];

        for (auto& e : g_reg) {
            if (e.gen < slot->gen) {
                slot = &e;
            }
        }
    }

    *slot = RegEntry{a, ++g_regGen};

    return true;
}

bool NeedsMigration(const uint8_t* grid) noexcept {
    if (!grid || g_regFault) {
        return false;  // after a failed registration no grid may be taken for stock-size
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(grid);

    for (const auto& e : g_reg) {
        if (e.addr == a) {
            return false;
        }
    }

    return true;
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

void OnGridCreated(uint8_t* grid) noexcept {
    if (!g_applied || g_failed) {
        return;
    }

    if (!MarkNative(grid)) {
        // Safe policy: an unregistered native grid would be migrated as if stock-size. Never migrate again.
        g_regFault = true;
        Rs7Log("fretgrid: registering native grid %p failed - migration disabled", grid);
    }
}

void SetDeferredApply(ApplyFn apply) {
    g_apply = apply;
    g_applied = g_failed = false;
    ResetRegistry();
    _InterlockedExchange(&g_ready, 1);
}

bool LayoutPending() {
    return g_rs7_native_fretgrid && !g_failed && g_rs7_fg_row != kRowNative;
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
    const uint32_t tail = kWireBaseStock + kFrets * kWireRowStock;  // 5C40h

    std::memcpy(fresh + tail, old + tail, kSizeStock - tail);
}

void OnGuitarUpdate(uint8_t* guitar) {
    if (g_failed || !guitar || !g_ready) {
        return;  // startup has not handed over the deferred sites yet
    }

    __try {
        if (!g_applied) {
            g_applied = true;

            if (!g_apply) {  // nothing deferred: the Fretgrid group was disabled at startup (cascade done there)
                g_failed = true;
                g_rs7_native_fretgrid = 0;
                Rs7Log("fretgrid: no deferred sites - native layout off");
                return;
            }

            if (!g_apply()) {
                g_failed = true;
                g_rs7_native_fretgrid = 0;
                Rs7Log("fretgrid: native layout NOT applied (a site could not be written; restored)");
                return;
            }

            g_rs7_fg_row = kRowNative;
            Rs7Log("fretgrid: native layout written on the game thread (guitar %p, first update)", guitar);
        }

        uint8_t** slot = reinterpret_cast<uint8_t**>(guitar + 0x10);
        uint8_t* old = *slot;

        if (NeedsMigration(old)) {
            uint8_t* fresh = static_cast<uint8_t*>(GameNew(kSizeNative));
            if (!fresh) {
                return;
            }

            GameCtor(fresh);
            MigrateGrid(old, fresh);
            *slot = fresh;
            MarkNative(fresh);
            GameFree(old, kSizeStock);
            ++g_migrated;
            Rs7Log("fretgrid: guitar %p: stock-size FretGrid %p replaced by native %p (%d so far)", guitar, old, fresh,
                   g_migrated);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Rs7Log("fretgrid: OnGuitarUpdate faulted (guitar %p)", guitar);
        g_failed = true;
    }
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
}  // namespace rs7fg

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FGHK(Rs7Mid::Context& c) {
    rs7colour::DrainPending();  // review #5: a colour publish requested by another thread, on the game thread
    rs7fg::OnGuitarUpdate(*reinterpret_cast<uint8_t**>(c.esp + 4));  // stdcall(guitar): [esp] = return address
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FGCT(Rs7Mid::Context& c) {
    // No C++ exception may reach the game's constructor frame; OnGridCreated is noexcept and applies the
    // fail-safe policy itself, this is the last line of defence.
    try {
        rs7fg::OnGridCreated(reinterpret_cast<uint8_t*>(c.eax));
    } catch (...) {
        rs7fg::DisableMigration();
    }
}

extern "C" void __cdecl Rs7Fg_BuildExtra(uint8_t* grid, uint8_t* guitar) {
    if (!g_rs7_native_fretgrid || !grid || !guitar) {
        return;
    }

    __try {
        const uint8_t* info = *reinterpret_cast<uint8_t* const*>(guitar + 0x24);
        if (!info || info[0xB] != rs7fg::kStock) {
            return;  // a 6-string guitar only (bass: 4 strings)
        }

        static int logged = 0;
        const int n = rs7fg::FillExtraCells(grid, info[0xC]);

        if (logged < 3) {
            ++logged;
            Rs7Log("fretgrid: grid %p, local points of cells 6/7 built for %d fret rows", grid, n);
        }
    } __except (1) {
    }
}
