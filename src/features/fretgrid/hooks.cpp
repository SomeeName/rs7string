// The native FretGrid's game glue (fretgrid.h): the switch and stride exports, the deferred layout write on the
// game thread, the stock -> native grid replacement through the game's allocator, and the FGHK / FGCT
// mid-hook callbacks (FGRB: hooks_build.cpp). The logic it calls is fretgrid.cpp.
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_native_fretgrid = 0;
__declspec(dllexport) uintptr_t g_rsxr_fg_row = 0x1CC;
}

namespace rsxrfg {
void Configure(bool native) {
    g_rsxr_native_fretgrid = native ? 1 : 0;
    g_rsxr_fg_row = native ? kRowNative : kRowStock;
}

void ConfigureDeferred(bool native) {
    g_rsxr_native_fretgrid = native ? 1 : 0;
    g_rsxr_fg_row = kRowStock;
}

namespace {
ApplyFn g_apply = nullptr;
bool g_applied = false, g_failed = false;
unsigned g_attempts = 0;  // deferred writes tried (ApplyResult::Retry: the freeze timed out)
DWORD g_retryAt = 0;      // GetTickCount of the next try after a Retry
// Set (last, with a barrier) by SetDeferredApply once startup has collected the deferred sites. FGHK is written
// with the main batch on the startup thread, so the game thread can reach OnGuitarUpdate before that: until
// then it must wait (CONFIRMED in the game, tests/full-test A_try3_stuck.log: the early update saw g_apply ==
// nullptr, switched native_fretgrid off silently and left native_publish / native_tuner waiting forever).
volatile long g_ready = 0;
int g_migrated = 0;
// Faults in OnGuitarUpdate after the layout was written. Migration must go on (a stock-size grid under the native
// stride is read past its end). After kMigrateFaults, stop trying and keep guitar updates skipped: resetting the
// stride variable alone cannot undo the native layout patches, and other guitars may already have native grids.
constexpr int kMigrateFaults = 3;
int g_migrateFaults = 0;

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

void* (*g_allocate)(uint32_t) = GameNew;
void* (*g_construct)(void*) = GameCtor;
void (*g_free)(void*, uint32_t) = GameFree;

}  // namespace

void SetGridAllocatorForTests(void* (*allocate)(uint32_t), void* (*construct)(void*), void (*free)(void*, uint32_t)) {
    g_allocate = allocate ? allocate : GameNew;
    g_construct = construct ? construct : GameCtor;
    g_free = free ? free : GameFree;
}

void OnGridCreated(uint8_t* grid) noexcept {
    if (!g_applied || g_failed) {
        return;
    }

    if (!MarkNative(grid)) {
        // Safe policy: an unregistered native grid would be migrated as if stock-size. Never migrate again.
        DisableMigration();
        RsxrLog("fretgrid: registering native grid %p failed - migration disabled", grid);
    }
}

void SetDeferredApply(ApplyFn apply) {
    g_apply = apply;
    g_applied = g_failed = false;
    g_attempts = 0;
    g_migrateFaults = 0;
    ResetRegistry();
    _InterlockedExchange(&g_ready, 1);
}

bool LayoutPending() {
    return g_rsxr_native_fretgrid && !g_failed && g_rsxr_fg_row != kRowNative;
}

bool OnGuitarUpdate(uint8_t* guitar) {
    if (g_failed || !guitar || !g_ready) {
        return !g_applied;  // after a migration failure the native patches still require a compatible grid
    }

    __try {
        if (!g_applied) {
            if (!g_apply) {  // nothing deferred: the Fretgrid group was disabled at startup (cascade done there)
                g_failed = true;
                g_rsxr_native_fretgrid = 0;
                RsxrLog("fretgrid: no deferred sites - native layout off");
                return true;
            }

            if (g_attempts && int(GetTickCount() - g_retryAt) < 0) {
                return true;  // the stock layout stays until the next try
            }

            const ApplyResult a = g_apply(g_attempts + 1 >= kApplyAttempts);

            ++g_attempts;

            if (a == ApplyResult::Retry && g_attempts < kApplyAttempts) {
                g_retryAt = GetTickCount() + kApplyRetryMs;
                RsxrLog("fretgrid: native layout not written yet (thread freeze timed out) - attempt %u of %u, next "
                        "in %u ms",
                        g_attempts, kApplyAttempts, kApplyRetryMs);
                return true;
            }

            if (a != ApplyResult::Applied) {
                g_failed = true;
                g_rsxr_native_fretgrid = 0;
                RsxrLog("fretgrid: native layout NOT applied (a site could not be written; restored)");
                return true;
            }

            g_applied = true;
            g_rsxr_fg_row = kRowNative;
            RsxrLog("fretgrid: native layout written on the game thread (guitar %p, first update)", guitar);
        }

        uint8_t** slot = reinterpret_cast<uint8_t**>(guitar + rsxr::kGuitarGrid);
        uint8_t* old = *slot;
        if (RegistryFailed() && !IsNative(old)) {
            return false;  // an unregistered grid may still be stock-size; no native access or sized migration
        }

        if (NeedsMigration(old)) {
            uint8_t* fresh = static_cast<uint8_t*>(g_allocate(kSizeNative));
            if (!fresh) {
                return false;
            }

            g_construct(fresh);
            MigrateGrid(old, fresh);
            *slot = fresh;
            MarkNative(fresh);
            g_free(old, kSizeStock);
            ++g_migrated;
            RsxrLog("fretgrid: guitar %p: stock-size FretGrid %p replaced by native %p (%d so far)", guitar, old, fresh,
                    g_migrated);
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        if (!g_applied) {
            RsxrLog("fretgrid: OnGuitarUpdate faulted before the layout switch (guitar %p)", guitar);
            g_failed = true;
        } else if (++g_migrateFaults < kMigrateFaults) {
            RsxrLog("fretgrid: OnGuitarUpdate faulted (guitar %p, %d of %d) - tried again on the next update", guitar,
                    g_migrateFaults, kMigrateFaults);
        } else {
            RsxrLog("fretgrid: OnGuitarUpdate faulted %d times (guitar %p) - grid migration stopped", kMigrateFaults,
                    guitar);
            g_failed = true;
        }

        return !g_applied;
    }

    return true;
}
}  // namespace rsxrfg

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_FGHK(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    rsxrcolour::DrainPending();  // a colour publish requested by another thread, on the game thread
    if (!rsxrfg::OnGuitarUpdate(*reinterpret_cast<uint8_t**>(c.esp + 4))) {
        RsxrMid::Ret(c, 4);  // stdcall(guitar): skip all native grid access, including after migration gives up
    }
}

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_FGCT(RsxrMid::Context& c) {
    // No C++ exception may reach the game's constructor frame; OnGridCreated is noexcept and applies the
    // fail-safe policy itself, this is the last line of defence.
    try {
        rsxrfg::OnGridCreated(reinterpret_cast<uint8_t*>(c.eax));
    } catch (...) {
        rsxrfg::DisableMigration();
    }
}

extern "C" void __cdecl RsxrFg_BuildExtra(uint8_t* grid, uint8_t* guitar) {
    // Cell() is native-stride: only once the layout is written (the switch is set while the stride is still stock)
    // and only for a grid built native (a stock-size grid built before the first FGHK is migrated later).
    if (!g_rsxr_native_fretgrid || !grid || !guitar || rsxrfg::RowStride() != rsxrfg::kRowNative ||
        !rsxrfg::IsNative(grid)) {
        return;
    }

    __try {
        const uint8_t* info = rsxr::GuitarInfo(guitar);
        if (!info || info[rsxr::kInfoStrings] != rsxrfg::kStock) {
            return;  // a 6-string guitar only (bass: 4 strings)
        }

        static int logged = 0;
        const int n = rsxrfg::FillExtraCells(grid, info[rsxr::kInfoFrets]);

        if (logged < 3) {
            ++logged;
            RsxrLog("fretgrid: grid %p, local points of cells 6/7 built for %d fret rows", grid, n);
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }
}
