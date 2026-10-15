#include "core/neck.h"
#include "core/publish.h"
#include "log.h"
#include <windows.h>

extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_spawn = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_adv3 = 0;

namespace {
using SpawnFn = void(__stdcall*)(void* note, void* self, uint32_t dtBits, uint32_t tBits, uint32_t string,
                                 uint32_t fret);

volatile LONG g_neckLogged = 0;

template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

// it = begin + 3 through the game's own iterator (0x7AA880: ESI = &it, clobbers EAX/ECX/EDX only).
uint8_t* Advance3(uint8_t* node) {
    uint8_t* it = node;

    __asm {
        push esi
        lea esi, it
        call dword ptr [g_rs7_fn_adv3]
        pop esi
    }

    return it;
}
}  // namespace

namespace {
// Prune + spawn of string 6 over the pointer row 6 (G+0x270) and a time row at G+timeOff.
int Row6(uint8_t* g, void* self, uint32_t dtBits, uint32_t tBits, uint32_t fretCount, uint32_t timeOff) {
    using namespace rs7neck;
    uint8_t** row = reinterpret_cast<uint8_t**>(g + kRow6Ptr);
    const float* time = reinterpret_cast<const float*>(g + timeOff);
    const uint32_t n = fretCount < kRowCells ? fretCount : kRowCells;
    bool any = false;

    for (uint32_t f = 0; f < n; ++f) {
        any = any || row[f] != nullptr;
    }

    if (!any) {
        return -1;
    }

    // Prune (0x7AA1B0): with 3+ distinct times, it = begin + 3 through the game's own iterator; if it != end,
    // clear every cell whose time is later than it's time (strictly; unordered compares keep the cell, like
    // fcomp + jne).
    if (At<uint32_t>(g, kSetSize) >= 3) {
        uint8_t* it = Advance3(At<uint8_t*>(g, kSetBegin));

        if (it != g + kSetHead) {
            const float limit = *reinterpret_cast<const float*>(it + kNodeTime);

            for (uint32_t f = 0; f < n; ++f) {
                if (row[f] && time[f] > limit) {
                    row[f] = nullptr;
                }
            }
        }
    }

    // Consumer (0x7E4965..0x7E49D4) for string 6: note+0x51 (single) or +0x52 (chord) set -> spawn.
    int first = -1;
    const SpawnFn spawn = reinterpret_cast<SpawnFn>(g_rs7_fn_spawn);

    for (uint32_t f = 0; f < n; ++f) {
        uint8_t* p = row[f];
        if (!p || (p[0x51] == 0 && p[0x52] == 0)) {
            continue;
        }

        spawn(p, self, dtBits, tBits, 6, f);
        if (first < 0 && f >= 1) {
            first = static_cast<int>(f);  // fret 0: Spawn returns at once (no target)
        }
    }

    return first;
}
}  // namespace

extern "C" int __cdecl Rs7_NeckRow6N(uint8_t* g, void* self, uint32_t dtBits, uint32_t tBits, uint32_t fretCount,
                                     uint32_t stringCount) {
    // The stock loops of 0x7E4870 enumerate rows below the active count; from 7 strings on row 6 is theirs.
    if (g_rs7_native_publish || stringCount > rs7neck::kStockStrings) {
        return -1;
    }

    return Row6(g, self, dtBits, tBits, fretCount, rs7neck::kNgRow6Time);
}

extern "C" void __cdecl Rs7_NeckLog(int fret) {
    try {
        if (InterlockedExchange(&g_neckLogged, 1) != 0) {
            return;
        }

        Rs7Log("neck: string-6 target spawned fret %d (first this song)", fret);
    } catch (...) {
        // Called from the NG9 stub inside a game frame: never let an exception out.
    }
}

void Rs7Neck_ResetLog() {
    InterlockedExchange(&g_neckLogged, 0);
}
