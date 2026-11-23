// The grid glue: GridBuild's cells 6/7 with its bounded wait, cell positions, finger span and edge string, called by
// the PUBG* / PUBEDGE / R1 mid-hooks (hooks_gridsites.cpp). The pure parts are grid.cpp.
#include "features/fretgrid/fretgrid.h"
#include "features/nslot/nslot.h"
#include "features/publish/hooks.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <iterator>
extern "C" int __cdecl RsxrGs_ExtraReady(uint8_t* guitar);
extern "C" int __cdecl RsxrGs_ExtraWhy(uint8_t* guitar);

extern "C" int __cdecl RsxrPublish_GridPos(uint8_t* grid, unsigned f, unsigned k, unsigned s, float* out) {
    __try {
        if (rsxrfg::RowStride() != rsxrfg::kRowNative) {
            return 0;
        }

        auto* guitar = *reinterpret_cast<uint8_t**>(grid);
        auto* info = *reinterpret_cast<uint8_t**>(guitar + rsxr::kGuitarInfo);

        return rsxrpubgrid::GetCellPos(grid, info[rsxr::kInfoStrings], f & 255, k, s & 255, out);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
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

extern "C" __declspec(dllexport) uintptr_t g_rsxr_publish_trace = 0;  // [debug] publish_trace
static_assert(offsetof(rsxrpubgrid::WaitEntry, owner) == 32 && offsetof(rsxrpubgrid::WaitEntry, failed) == 36 &&
                  sizeof(rsxrpubgrid::WaitEntry) == 40,
              "tools/tests/test_publish_state_emu.py writes this layout");
// Exported so the tests can see (and set) the failed-neck state; the layout is the WaitEntry of publish_grid.h.
extern "C" __declspec(dllexport) rsxrpubgrid::WaitEntry g_rsxr_publish_wait[8] = {};

namespace {
constexpr uint64_t kWaitMs = 5000;  // GridWait: not ready this long -> the stock completion
static_assert(kWaitMs == 5000, "the give-up log lines in RsxrPublish_GridWait say 5 s");
constexpr uint32_t kInfoFrets = 0x0C;          // InstrumentInfo: u8 fret count
constexpr uint32_t kGuitarWorldOffset = 0x2C;  // Rnd_Guitar: float[3] offset added to the local cell positions
constexpr size_t kWaitSlots = std::size(g_rsxr_publish_wait);

const char* const kWhy[] = {"ready",
                            "FretGrid layout not written yet",
                            "extra strings 6/7 not ready",
                            "string 6/7 model not ready",
                            "fret count > 26",
                            "cells 0/1 of a fret row not valid",
                            "fault while checking readiness"};
}  // namespace

// Not-ready bookkeeping (Win32 clock + log: the emulator replaces it with `xor eax,eax; ret` = keep waiting).
// Rnd_FretGrid_Load (0x58F260, CONFIRMED caller of BuildCells 0x58F8A0) is polled until it returns 2; a
// "not ready" that never clears would hold the song load forever. After kWaitMs the grid gets the stock
// completion with no cells 6/7 staged. A guitar built with 7/8 strings then has no neck geometry for them: its
// neck is marked FAILED (RsxrPublish_NeckFailed): the song's detector is not widened (the song stays a
// six-string song for scoring), and the next clite selection rebuilds the guitar (GKT treats a failed guitar as
// not matching any desired count). A guitar built with 6 strings or the boot guitar, whose stock-size grid is
// replaced by the native one at its first update, is not affected: the string count the guitar was built with decides.
static const uint8_t* GuitarOf(const uint8_t* grid) {
    const uint8_t* guitar = nullptr;

    __try {
        guitar = *reinterpret_cast<uint8_t* const*>(grid);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        guitar = nullptr;
    }

    return guitar;
}

static unsigned BuiltCount(const uint8_t* grid) {
    unsigned n = 0;

    __try {
        const uint8_t* guitar = *reinterpret_cast<uint8_t* const*>(grid);

        n = (*reinterpret_cast<uint8_t* const*>(guitar + rsxr::kGuitarInfo))[rsxr::kInfoStrings];
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }

    return n;
}

extern "C" int __cdecl RsxrPublish_NeckFailed(const void* guitar) {
    return rsxrpubgrid::Failed(g_rsxr_publish_wait, kWaitSlots, guitar) ? 1 : 0;
}

extern "C" void __cdecl RsxrPublish_GridForget(const void* guitar) {
    if (guitar) {
        rsxrpubgrid::WaitDone(g_rsxr_publish_wait, kWaitSlots, nullptr, guitar);
    }
}

extern "C" void __cdecl RsxrPublish_GridForgetAll() {
    for (auto& e : g_rsxr_publish_wait) {
        e = rsxrpubgrid::WaitEntry{};
    }
}

extern "C" __declspec(noinline) int __cdecl RsxrPublish_GridWait(uint8_t* grid, unsigned reason) {
    const uint64_t now = GetTickCount64();
    const bool giveUp =
        rsxrpubgrid::WaitStep(g_rsxr_publish_wait, kWaitSlots, grid, reason, now, kWaitMs, GuitarOf(grid));

    if (giveUp) {
        const unsigned b = BuiltCount(grid);

        if (rsxr::IsExtendedGuitar(b)) {
            rsxrpubgrid::MarkFailed(g_rsxr_publish_wait, kWaitSlots, grid);
        }
    }

    static volatile long lines = 0;
    static uint64_t last = 0;

    if (giveUp || (g_rsxr_publish_trace && now - last >= 1000)) {
        last = now;
        unsigned calls = 0;

        for (auto& e : g_rsxr_publish_wait) {
            if (e.grid == grid) {
                calls = e.calls;
            }
        }

        if (RsxrLog_Upto(&lines, 100)) {
            const unsigned built = giveUp ? BuiltCount(grid) : 0;

            RsxrLog(
                "native_publish: GridBuild grid %p not ready (%s, detail %u), %u calls%s", grid,
                reason < 7        ? kWhy[reason]
                : reason >= 0x100 ? kWhy[2]
                                  : "?",
                reason & 0xFF, calls,
                !giveUp ? ""
                : rsxr::IsExtendedGuitar(built)
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
    return RsxrPublish_GridWait(grid, reason) ? 2 : 1;
}

extern "C" int __cdecl RsxrPublish_GridBuild(uint8_t* grid, uint8_t* guitar) {
    __try {
        auto* info = *reinterpret_cast<uint8_t**>(guitar + rsxr::kGuitarInfo);
        if (rsxr::IsBassKind(info[rsxr::kInfoKind]) || info[rsxr::kInfoStrings] < 6) {
            return 2;  // bass retains the stock completion
        }
        // native_fretgrid off or failed (its cascade turns native_publish off, but this site stays written):
        // the stock completion. Waiting here would hold Rnd_FretGrid_Load forever.
        if (!g_rsxr_native_fretgrid) {
            return 2;
        }
        // The native cell rows exist only once the FretGrid layout is written (deferred, features/fretgrid/fretgrid.h);
        // a stock-size grid is migrated at the next guitar update: not ready yet.
        if (rsxrfg::RowStride() != rsxrfg::kRowNative) {
            return NotReady(grid, 1);
        }

        auto* gs = *reinterpret_cast<uint8_t**>(guitar + rsxr::kGuitarStrings);
        if (!RsxrGs_ExtraReady(guitar)) {
            return NotReady(grid, 0x100 | unsigned(RsxrGs_ExtraWhy(guitar)));
        }

        for (int s = 6; s < 8; ++s) {
            if (!ModelReady(rsxrgs::Child(gs, s, false))) {
                return NotReady(grid, 3);
            }
        }

        const unsigned frets = info[kInfoFrets];
        if (frets > 26) {
            return NotReady(grid, 4);
        }
        if (rsxrfg::FillExtraCells(grid, frets) != int(frets)) {
            return NotReady(grid, 5);
        }

        rsxrpubgrid::WaitDone(g_rsxr_publish_wait, kWaitSlots, grid,
                              guitar);  // ready: this build's wait and any failure are over

        // Stage real world cells now as well: count publication may precede the next offset pass.
        // Always local+offset, never offset an already transformed world value.
        for (unsigned f = 0; f < frets; ++f) {
            for (unsigned s = 6; s < 8; ++s) {
                auto* c = rsxrfg::Cell(grid, f, s);

                for (unsigned k = 0; k < 3; ++k) {
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        float local, offset;

                        std::memcpy(&local, c + rsxrfg::kCellLocal + k * 12 + axis * 4, 4);
                        std::memcpy(&offset, guitar + kGuitarWorldOffset + axis * 4, 4);
                        const float world = local + offset;

                        std::memcpy(c + rsxrfg::kCellWorld + k * 12 + axis * 4, &world, 4);
                    }
                }
            }
        }

        return 2;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        // A fault (bad info/child pointer, model readiness) is a not-ready like any other: it goes through the
        // same per-grid deadline, so a persistent fault ends in the stock completion after kWaitMs instead of
        // holding Rnd_FretGrid_Load forever.
        return NotReady(grid, 6);
    }
}

// The marker being placed belongs to an extended bass (5/6 strings): set by RsxrPublish_FingerAnchor (PUBFA) at the
// entry of the marker function, read by the span (PUBGF) that the same call makes next on the same thread.
extern "C" __declspec(dllexport) volatile uint32_t g_rsxr_finger_bass = 0;

extern "C" int __cdecl RsxrPublish_FingerSpan(const uint8_t* tpl, unsigned finger, int* span) {
    const uint8_t* t8 = RsxrNs_AsT8(tpl);
    const bool bass = g_rsxr_finger_bass != 0;

    if (t8) {
        unsigned n = t8[rsxrns::kT8Slots];

        if (n < 6 || n > 8) {
            n = 6;
        }
        return rsxrpubgrid::FingerSpan(t8 + rsxrns::kT8Fingers, n, finger, span, bass);
    }

    return rsxrpubgrid::FingerSpan(tpl + 0xA, 6, finger, span, bass);
}

// `owner` is the marker function's owner (EDI at its entry): +6Ch the player, whose guitar gives the instrument.
// Six-string guitar and four-string bass keep the stock anchor (slot order is physical order there).
extern "C" unsigned __cdecl RsxrPublish_FingerAnchor(const uint8_t* tpl, unsigned string, const uint8_t* owner) {
    const uint8_t* player = owner ? *reinterpret_cast<const uint8_t* const*>(owner + 0x6C) : nullptr;
    const uint8_t* guitar = player ? *reinterpret_cast<const uint8_t* const*>(player + rsxr::kSessionGuitar) : nullptr;
    const uint8_t* info = rsxr::GuitarInfo(guitar);
    const bool bass = info && rsxr::IsBassKind(info[rsxr::kInfoKind]) && info[rsxr::kInfoStrings] > 4;
    const uint8_t* t8 = RsxrNs_AsT8(tpl);

    g_rsxr_finger_bass = bass ? 1 : 0;

    if (bass) {
        return rsxrpubgrid::FingerAnchor(t8 ? t8 + rsxrns::kT8Fingers : tpl + 0xA, 6, string, true);
    }

    if (!t8) {
        return string;
    }

    const unsigned n = t8[rsxrns::kT8Slots];

    return n == 7 || n == 8 ? rsxrpubgrid::FingerAnchor(t8 + rsxrns::kT8Fingers, n, string) : string;
}

extern "C" unsigned __cdecl RsxrPublish_Edge(uint8_t* info, unsigned first) {
    return rsxrpubgrid::PhysicalEdge(info[rsxr::kInfoStrings], first != 0, rsxr::IsBassKind(info[rsxr::kInfoKind]));
}

// The TU8A copy of StringAction (features/tuner/hooks.h) contains the PUBEDGE site (0x7E72D0): same expression,
// over the native active count (+40h); the physical 7/8-string edge only while native_publish is on.
extern "C" unsigned __cdecl RsxrPublish_EdgeCopy(uint8_t* info, unsigned first) {
    const unsigned n = info[rsxrgs::kInfoActive];
    if (g_rsxr_publish_ports) {
        return rsxrpubgrid::PhysicalEdge(n, first != 0, rsxr::IsBassKind(info[rsxr::kInfoKind]));
    }

    return first ? 0u : ((n - 1) & 0xFFu);
}
