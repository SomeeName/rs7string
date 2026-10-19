#include "features/diag/diag.h"
#include "features/fretgrid/fretgrid.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

namespace {
volatile LONG g_hits[kRs7DiagPointCount][2] = {};  // [point][0 = string 0, 1 = string 6]

// Copies n bytes of game memory; false (instead of an access violation) if unreadable. Only access
// violations are handled: anything else (guard page, in-page error, ...) belongs to the game.
// No C++ objects here, so SEH is allowed.
bool SafeCopy(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

// A dword of game memory without memcpy (the CRT's has jump tables the sse-mode x87 scan cannot verify): for the D1
// producer, which runs inside R2's sse-mode callback.
bool ReadDword(const uint8_t* base, uint32_t off, uint32_t* out) {
    if (!base) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<const volatile uint32_t*>(base + off);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

template <class T>
bool Read(const uint8_t* base, uint32_t off, T* out) {
    return base && SafeCopy(out, base + off, sizeof(T));
}

unsigned Va(const void* p) {
    return unsigned(reinterpret_cast<uintptr_t>(p));
}
}  // namespace

namespace {
volatile LONG* Counter(int point, uint32_t string) {
    if (point < 0 || point >= kRs7DiagPointCount) {
        return nullptr;
    }

    const int slot = string == 0 ? 0 : string == 6 ? 1 : -1;

    return slot < 0 ? nullptr : &g_hits[point][slot];
}
}  // namespace

bool Rs7Diag_Take(int point, uint32_t string) {
    volatile LONG* n = Counter(point, string);
    if (!n || *n >= kRs7DiagMaxHits) {
        return false;  // no further increments: the counter cannot wrap
    }

    return InterlockedIncrement(n) <= kRs7DiagMaxHits;
}

bool Rs7Diag_Peek(int point, uint32_t string) {
    const volatile LONG* n = Counter(point, string);

    return n && *n < kRs7DiagMaxHits;
}

void Rs7Diag_Reset() {
    for (auto& point : g_hits) {
        for (auto& n : point) {
            InterlockedExchange(&n, 0);
        }
    }
}

namespace {
// D1 lines waiting for a full-mode context (Rs7Diag_FlushNoteHeads): R2 runs in sse mode with the game's x87 stack
// live, where the CRT formatting of Rs7Log must not run. Lock-free, any number of producers and consumers: a slot is
// 0 free, 1 being written, 2 full, 3 being read. A full ring drops the line (diagnostics only).
struct NoteHeadLine {
    volatile LONG state;
    uint32_t string, fret, open, z0, z1, z, floor, absCz;
};

constexpr LONG kNoteHeadSlots = 16;
NoteHeadLine g_noteHeads[kNoteHeadSlots] = {};
volatile LONG g_noteHeadNext = 0;
}  // namespace

// Integer only (no CRT, no logging, no C++ exception frame): it runs inside R2's sse-mode callback.
extern "C" void __cdecl Rs7_DiagNoteHead(uint32_t string, uint32_t fret, uint32_t open, const uint8_t* row,
                                         uint32_t floorBits, uint32_t zBits, uint32_t absCzBits) {
    string &= 0xFF;
    uint32_t z0 = 0, z1 = 0;

    if (!Rs7Diag_Peek(kRs7D1, string) || !ReadDword(row, 0x44, &z0) || !ReadDword(row, 0x90, &z1)) {
        return;
    }

    NoteHeadLine& line = g_noteHeads[(InterlockedIncrement(&g_noteHeadNext) & 0x7fffffff) % kNoteHeadSlots];

    if (InterlockedCompareExchange(&line.state, 1, 0) != 0) {
        return;  // the ring is full
    }

    if (!Rs7Diag_Take(kRs7D1, string)) {
        InterlockedExchange(&line.state, 0);
        return;
    }

    line.string = string, line.fret = fret & 0xFF, line.open = open & 0xFF, line.z0 = z0, line.z1 = z1;
    line.z = zBits, line.floor = floorBits, line.absCz = absCzBits;
    InterlockedExchange(&line.state, 2);
}

namespace {
// Not inlined: its exception frame is set up only when there is a line (the flush itself writes nothing when the ring
// is empty, so the hooks that call it stay observably unchanged).
__declspec(noinline) void LogNoteHead(const NoteHeadLine& line) {
    try {
        Rs7Log("D1 head lane: string %u fret %u open %u z0 %08X z1 %08X z %08X floor %08X |Cz| %08X", line.string,
               line.fret, line.open, line.z0, line.z1, line.z, line.floor, line.absCz);
    } catch (...) {
    }
}
}  // namespace

void Rs7Diag_FlushNoteHeads() {
    for (NoteHeadLine& line : g_noteHeads) {
        // a plain read first: a failed lock cmpxchg still writes the slot
        if (line.state != 2 || InterlockedCompareExchange(&line.state, 3, 2) != 2) {
            continue;
        }

        const NoteHeadLine copy = line;

        InterlockedExchange(&line.state, 0);
        LogNoteHead(copy);
    }
}

extern "C" void __cdecl Rs7_DiagGemCreate(uint32_t caller, uint32_t model, uint32_t note, uint32_t fret,
                                          uint32_t string, uint32_t type) {
    try {
        string &= 0xFF;
        if (!Rs7Diag_Take(kRs7D2, string)) {
            return;
        }

        Rs7Log("D2 gem create: string %u fret %u type %u model %08X note %08X caller %08X", string, fret & 0xFF, type,
               model, note, caller);
    } catch (...) {
    }
}

extern "C" void __cdecl Rs7_DiagGeomBuild(const uint8_t* geom) {
    try {
        // The string is only known after two reads: skip them once both strings are spent.
        if (!Rs7Diag_Peek(kRs7D3, 0) && !Rs7Diag_Peek(kRs7D3, 6)) {
            return;
        }

        const uint8_t* gem = nullptr;
        uint8_t sf[2] = {};
        uint32_t type = 0, mode = 0, v[9] = {};
        if (!Read(geom, 0x8, &gem) || !Read(gem, 0xC, &sf) || !Rs7Diag_Peek(kRs7D3, sf[0])) {
            return;
        }
        if (!Read(gem, 0x10, &type) || !Read(geom, 0x14, &mode) || !Read(geom, 0x20, &v)) {
            return;
        }
        if (!Rs7Diag_Take(kRs7D3, sf[0])) {
            return;
        }

        Rs7Log("D3 geom build: string %u fret %u type %u mode %u gem %08X geom %08X v20 %08X %08X %08X "
               "v2C %08X %08X %08X v38 %08X %08X %08X",
               sf[0], sf[1], type, mode, Va(gem), Va(geom), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
    } catch (...) {
    }
}

extern "C" void __cdecl Rs7_DiagCellPos(uint32_t caller, const uint8_t* grid, uint32_t fret, uint32_t k,
                                        uint32_t string, const uint32_t* out) {
    try {
        string &= 0xFF;
        fret &= 0xFF;
        if (!Rs7Diag_Peek(kRs7D4, string)) {
            return;  // also filters strings other than 0 and 6
        }

        uint32_t v[3] = {};

        if (out) {
            if (!SafeCopy(v, out, sizeof v)) {
                return;
            }
        } else {
            const uint32_t f1 = (fret - 1) & 0xFF;  // stock: (u8)(fret - 1) <= 0x19, else no cell
            if (f1 > 0x19 || !Read(grid, 0x30 + f1 * rs7fg::RowStride() + k * 0xC, &v)) {
                return;
            }
        }

        if (!Rs7Diag_Take(kRs7D4, string)) {
            return;
        }

        Rs7Log("D4 cell pos: string %u fret %u k %u out %08X %08X %08X caller %08X", string, fret, k, v[0], v[1], v[2],
               caller);
    } catch (...) {
    }
}

extern "C" void __cdecl Rs7_DiagGemPos(const uint8_t* gem, const uint32_t* pos) {
    try {
        // The string is only known after one read: skip it once both strings are spent.
        if (!Rs7Diag_Peek(kRs7D5, 0) && !Rs7Diag_Peek(kRs7D5, 6)) {
            return;
        }

        uint8_t sf[2] = {};
        uint32_t type = 0, offset[3] = {}, p[3] = {};
        if (!Read(gem, 0xC, &sf) || !Rs7Diag_Peek(kRs7D5, sf[0])) {
            return;
        }
        if (!Read(gem, 0x10, &type) || !Read(gem, 0x14, &offset) || !SafeCopy(p, pos, sizeof p)) {
            return;
        }
        if (!Rs7Diag_Take(kRs7D5, sf[0])) {
            return;
        }

        Rs7Log("D5 gem pos: string %u fret %u type %u gem %08X offset %08X %08X %08X pos %08X %08X %08X", sf[0], sf[1],
               type, Va(gem), offset[0], offset[1], offset[2], p[0], p[1], p[2]);
    } catch (...) {
    }
}
