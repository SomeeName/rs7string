// The grid glue: GridBuild's cells 6/7 with its bounded wait, cell positions, finger span and edge string, called by
// the PUBG* / PUBEDGE / R1 mid-hooks (hooks_gridsites.cpp). The pure parts are grid.cpp.
#include "features/fretgrid/fretgrid.h"
#include "features/nslot/nslot.h"
#include "features/publish/hooks.h"
#include "features/strings/strings.h"
#include "platform/log.h"
#include <windows.h>
#include <cstddef>
#include <cstring>
extern "C" int __cdecl Rs7Gs_ExtraReady(uint8_t* guitar);
extern "C" int __cdecl Rs7Gs_ExtraWhy(uint8_t* guitar);

extern "C" int __cdecl Rs7Publish_GridPos(uint8_t* grid, unsigned f, unsigned k, unsigned s, float* out) {
    __try {
        if (rs7fg::RowStride() != rs7fg::kRowNative) {
            return 0;
        }

        auto* guitar = *reinterpret_cast<uint8_t**>(grid);
        auto* info = *reinterpret_cast<uint8_t**>(guitar + 0x24);

        return rs7pubgrid::GetCellPos(grid, info[0xB], f & 255, k, s & 255, out);
    } __except (1) {
        return 0;
    }
}

// Same readiness checks as 58F8DD..58F915; register ABI of 411130/9BD3F0 retained.
static int ModelReady(uint8_t* child) {
    int ready = 0;

    __asm {
        push esi
        mov eax, child
        mov esi, [eax+4]
        test esi, esi
        jz done
        mov edx, 411130h
        call edx
        cmp dword ptr [eax+10Ch], 0
        je done
        test byte ptr [esi+0D8h], 1
        jz query_root
        mov eax, [esi+0DCh]
        jmp check_root
    query_root:
        push eax
        mov ecx, esi
        mov edx, 9BD3F0h
        call edx
    check_root:
        test eax,eax
        setnz al
        movzx eax,al
        mov ready,eax
    done:
        pop esi
    }

    return ready;
}

extern "C" __declspec(dllexport) uintptr_t g_rs7_publish_trace = 0;  // [debug] publish_trace
static_assert(offsetof(rs7pubgrid::WaitEntry, owner) == 32 && offsetof(rs7pubgrid::WaitEntry, failed) == 36 &&
                  sizeof(rs7pubgrid::WaitEntry) == 40,
              "tools/tests/test_publish_state_emu.py writes this layout");
// Exported so the tests can see (and set) the failed-neck state; the layout is the WaitEntry of publish_grid.h.
extern "C" __declspec(dllexport) rs7pubgrid::WaitEntry g_rs7_publish_wait[8] = {};

namespace {
const char* const kWhy[] = {"ready",
                            "FretGrid layout not written yet",
                            "extra strings 6/7 not ready",
                            "string 6/7 model not ready",
                            "fret count > 26",
                            "cells 0/1 of a fret row not valid",
                            "fault while checking readiness"};
}

// Not-ready bookkeeping (Win32 clock + log: the emulator replaces it with `xor eax,eax; ret` = keep waiting).
// Rnd_FretGrid_Load (0x58F260, CONFIRMED caller of BuildCells 0x58F8A0) is polled until it returns 2; a
// "not ready" that never clears would hold the song load forever. After kWaitMs the grid gets the stock
// completion with no cells 6/7 staged. A guitar built with 7/8 strings then has no neck geometry for them: its
// neck is marked FAILED (review F3, Rs7Publish_NeckFailed): the song's detector is not widened (the song stays a
// six-string song for scoring), and the next clite selection rebuilds the guitar (GKT treats a failed guitar as
// not matching any desired count). A guitar built with 6 strings or the boot guitar, whose stock-size grid is
// replaced by the native one at its first update, is not affected: the string count the guitar was built with decides.
static const uint8_t* GuitarOf(const uint8_t* grid) {
    const uint8_t* guitar = nullptr;

    __try {
        guitar = *reinterpret_cast<uint8_t* const*>(grid);
    } __except (1) {
        guitar = nullptr;
    }

    return guitar;
}

static unsigned BuiltCount(const uint8_t* grid) {
    unsigned n = 0;

    __try {
        const uint8_t* guitar = *reinterpret_cast<uint8_t* const*>(grid);

        n = (*reinterpret_cast<uint8_t* const*>(guitar + 0x24))[0xB];
    } __except (1) {
    }

    return n;
}

extern "C" int __cdecl Rs7Publish_NeckFailed(const void* guitar) {
    return rs7pubgrid::Failed(g_rs7_publish_wait, 8, guitar) ? 1 : 0;
}

extern "C" void __cdecl Rs7Publish_GridForgetAll() {
    for (auto& e : g_rs7_publish_wait) {
        e = rs7pubgrid::WaitEntry{};
    }
}

extern "C" __declspec(noinline) int __cdecl Rs7Publish_GridWait(uint8_t* grid, unsigned reason) {
    constexpr uint64_t kWaitMs = 5000;
    const uint64_t now = GetTickCount64();
    const bool giveUp = rs7pubgrid::WaitStep(g_rs7_publish_wait, 8, grid, reason, now, kWaitMs, GuitarOf(grid));

    if (giveUp) {
        const unsigned b = BuiltCount(grid);

        if (b == 7 || b == 8) {
            rs7pubgrid::MarkFailed(g_rs7_publish_wait, 8, grid);
        }
    }

    static volatile long lines = 0;
    static uint64_t last = 0;

    if (giveUp || (g_rs7_publish_trace && now - last >= 1000)) {
        last = now;
        unsigned calls = 0;

        for (auto& e : g_rs7_publish_wait) {
            if (e.grid == grid) {
                calls = e.calls;
            }
        }

        if (_InterlockedIncrement(&lines) <= 100) {
            const unsigned built = giveUp ? BuiltCount(grid) : 0;

            Rs7Log("native_publish: GridBuild grid %p not ready (%s, detail %u), %u calls%s", grid,
                   reason < 7        ? kWhy[reason]
                   : reason >= 0x100 ? kWhy[2]
                                     : "?",
                   reason & 0xFF, calls,
                   !giveUp ? ""
                   : built == 7 || built == 8
                       ? " - FAIL: waited >= 5 s, stock completion without cells 6/7: the neck of this guitar is "
                         "marked failed (song detector stays six strings, next selection rebuilds the guitar)"
                       : " - waited >= 5 s: stock completion (this guitar was not built with 7/8 strings: no cells 6/7 "
                         "needed)");
        }
    }

    // A grid that was given up stays given up (the step can be polled again) until it becomes ready.
    return giveUp ? 1 : 0;
}

static int NotReady(uint8_t* grid, unsigned reason) {
    return Rs7Publish_GridWait(grid, reason) ? 2 : 1;
}

extern "C" int __cdecl Rs7Publish_GridBuild(uint8_t* grid, uint8_t* guitar) {
    __try {
        auto* info = *reinterpret_cast<uint8_t**>(guitar + 0x24);
        if (info[0xB] < 6) {
            return 2;  // bass retains the stock completion
        }
        // native_fretgrid off or failed (its cascade turns native_publish off, but this site stays written):
        // the stock completion. Waiting here would hold Rnd_FretGrid_Load forever.
        if (!g_rs7_native_fretgrid) {
            return 2;
        }
        // The native cell rows exist only once the FretGrid layout is written (deferred, features/fretgrid/fretgrid.h);
        // a stock-size grid is migrated at the next guitar update: not ready yet.
        if (rs7fg::RowStride() != rs7fg::kRowNative) {
            return NotReady(grid, 1);
        }

        auto* gs = *reinterpret_cast<uint8_t**>(guitar + 0xC);
        if (!Rs7Gs_ExtraReady(guitar)) {
            return NotReady(grid, 0x100 | unsigned(Rs7Gs_ExtraWhy(guitar)));
        }

        for (int s = 6; s < 8; ++s) {
            if (!ModelReady(rs7gs::Child(gs, s, false))) {
                return NotReady(grid, 3);
            }
        }

        const unsigned frets = info[0xC];
        if (frets > 26) {
            return NotReady(grid, 4);
        }
        if (rs7fg::FillExtraCells(grid, frets) != int(frets)) {
            return NotReady(grid, 5);
        }

        rs7pubgrid::WaitDone(g_rs7_publish_wait, 8, grid, guitar);  // ready: this build's wait and any failure are over

        // Stage real world cells now as well: count publication may precede the next offset pass.
        // Always local+offset, never offset an already transformed world value.
        for (unsigned f = 0; f < frets; ++f) {
            for (unsigned s = 6; s < 8; ++s) {
                auto* c = rs7fg::Cell(grid, f, s);

                for (unsigned k = 0; k < 3; ++k) {
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        float local, offset;

                        std::memcpy(&local, c + rs7fg::kCellLocal + k * 12 + axis * 4, 4);
                        std::memcpy(&offset, guitar + 0x2C + axis * 4, 4);
                        const float world = local + offset;

                        std::memcpy(c + rs7fg::kCellWorld + k * 12 + axis * 4, &world, 4);
                    }
                }
            }
        }

        return 2;
    } __except (1) {
        // A fault (bad info/child pointer, model readiness) is a not-ready like any other: it goes through the
        // same per-grid deadline, so a persistent fault ends in the stock completion after kWaitMs instead of
        // holding Rnd_FretGrid_Load forever.
        return NotReady(grid, 6);
    }
}

extern "C" int __cdecl Rs7Publish_FingerSpan(const uint8_t* tpl, unsigned finger, int* span) {
    const uint8_t* t8 = Rs7Ns_AsT8(tpl);

    if (t8) {
        unsigned n = t8[0x78];

        if (n < 6 || n > 8) {
            n = 6;
        }
        return rs7pubgrid::FingerSpan(t8 + 0x50, n, finger, span);
    }

    return rs7pubgrid::FingerSpan(tpl + 0xA, 6, finger, span);
}

extern "C" unsigned __cdecl Rs7Publish_Edge(uint8_t* info, unsigned first) {
    return rs7pubgrid::PhysicalEdge(info[0xB], first != 0);
}

// The TU8A copy of StringAction (features/tuner/hooks.h) contains the PUBEDGE site (0x7E72D0): same expression,
// over the native active count (+40h); the physical 7/8-string edge only while native_publish is on.
extern "C" unsigned __cdecl Rs7Publish_EdgeCopy(uint8_t* info, unsigned first) {
    const unsigned n = info[0x40];
    if (g_rs7_publish_ports) {
        return rs7pubgrid::PhysicalEdge(n, first != 0);
    }

    return first ? 0u : ((n - 1) & 0xFFu);
}
