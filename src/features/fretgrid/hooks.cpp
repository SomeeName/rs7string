// The native FretGrid's game glue (fretgrid.h): the switch and stride exports, the deferred layout write on the
// game thread, the stock -> native grid replacement through the game's allocator, and the FGHK / FGCT
// mid-hook callbacks (FGRB: hooks_build.cpp). The logic it calls is fretgrid.cpp.
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "platform/log.h"
#include <windows.h>

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

void OnGridCreated(uint8_t* grid) noexcept {
    if (!g_applied || g_failed) {
        return;
    }

    if (!MarkNative(grid)) {
        // Safe policy: an unregistered native grid would be migrated as if stock-size. Never migrate again.
        DisableMigration();
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
}  // namespace rs7fg

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FGHK(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

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
