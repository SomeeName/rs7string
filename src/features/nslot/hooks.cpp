#include "features/allocstats/allocstats.h"
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "features/publish/publish.h"
#include "model/eight.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

extern "C" __declspec(dllexport) Rs7NsEntry g_rs7_ns[kRs7NsEntries] = {};

namespace {

using namespace rs7ns;

SRWLOCK g_lock = SRWLOCK_INIT;  // writers only (loader thread, song destructor); readers are lock-free
volatile LONG g_logged = 0;

bool SafeCopy(void* dst, const void* src, size_t n) {
    if (!dst || !src) {
        return false;
    }

    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct Vec {
    uintptr_t begin = 0, end = 0;
};

bool ReadVec(const void* song, uint32_t off, Vec* v) {
    return SafeCopy(v, static_cast<const uint8_t*>(song) + off, sizeof *v) && v->end >= v->begin;
}

uint8_t* Alloc(Rs7AllocKind kind, size_t n) {
    uint8_t* p = n ? static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n)) : nullptr;

    if (p) {
        Rs7Alloc_Inc(kind);
    }

    return p;
}

void Free(Rs7AllocKind kind, uintptr_t p) {
    if (!p) {
        return;
    }

    HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(p));
    Rs7Alloc_Dec(kind);
}

Rs7NsEntry* FindLocked(uintptr_t song) {
    for (auto& e : g_rs7_ns) {
        if (e.song == song) {
            return &e;
        }
    }

    return nullptr;
}

// Writer lock held. Preserve song/origCn so a failed post-process can retry.
void DropArraysLocked(Rs7NsEntry* e) {
    // Unpublish first (readers compare the begin pointers), then free.
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->tplBegin), 0);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->cnBegin), 0);
    MemoryBarrier();
    Free(kAkNsT8, e->t8);
    Free(kAkNsC8, e->c8);
    e->t8 = e->c8 = 0;
    e->tplCount = e->cnCount = 0;
}

void ClearLocked(Rs7NsEntry* e) {
    DropArraysLocked(e);
    e->origCn = 0;
    e->song = 0;
}

struct SongView {
    Vec tpl, cn, tuning, levels;
    uint8_t capo = 0;
    int16_t tun[16] = {};
    int n = 0;
};

bool ReadSong(const void* song, SongView* v) {
    if (!ReadVec(song, 0x94, &v->tpl) || !ReadVec(song, 0xAC, &v->cn) || !ReadVec(song, 0x110, &v->tuning) ||
        !ReadVec(song, 0x40, &v->levels) || !SafeCopy(&v->capo, static_cast<const uint8_t*>(song) + 0x11C, 1)) {
        return false;
    }

    // A stock vector may fit in the address space while its larger T8/C8 array does not.
    // Reject partial records and validate counts before the 32-bit allocation products.
    const size_t templates = v->tpl.end - v->tpl.begin, chordNotes = v->cn.end - v->cn.begin;
    if (templates % kTplSize || chordNotes % kCnSize || templates / kTplSize > SIZE_MAX / kT8Size ||
        chordNotes / kCnSize > SIZE_MAX / kC8Size || (v->tuning.end - v->tuning.begin) % 2 ||
        (v->levels.end - v->levels.begin) % 100) {
        return false;
    }

    v->n = static_cast<int>((v->tuning.end - v->tuning.begin) / 2);
    if (v->n > 16) {
        v->n = 16;
    }

    return v->n == 0 || SafeCopy(v->tun, reinterpret_cast<const void*>(v->tuning.begin), static_cast<size_t>(v->n) * 2);
}

// Extended columns of template t: the RS7X block, else (7-string arrangements) the old name[31].
void ExtCols(const rs7ext::Block* b, const uint8_t* tpl, uint32_t t, int n, Col ext[2], bool* fromName) {
    ext[0] = ext[1] = Col();
    *fromName = false;

    if (b) {
        for (int i = 0; i < 2; ++i) {
            int f, g;

            if (b->TemplateColumn(t, 6 + i, &f, &g)) {
                ext[i] = Col{f, g};
            }
        }

        return;
    }

    if (n == 7 && DecodeName31(tpl, &ext[0])) {
        *fromName = true;
    }
}

// Builds (or rebuilds in place) the T8 array of a song. Returns the number of templates with an
// extended column.
int BuildTemplates(Rs7NsEntry* e, const SongView& v, const rs7ext::Block* b, int* fromName) {
    const uint32_t count = static_cast<uint32_t>((v.tpl.end - v.tpl.begin) / kTplSize);

    *fromName = 0;
    // Called with the writer lock held: failure must return normally so the caller unlocks.
    std::unique_ptr<uint8_t[]> stock(new (std::nothrow) uint8_t[static_cast<size_t>(count) * kTplSize + 1]);
    if (!stock) {
        return -1;
    }
    if (count && !SafeCopy(stock.get(), reinterpret_cast<const void*>(v.tpl.begin), count * kTplSize)) {
        return -1;
    }

    if (!e->t8 || e->tplCount != count) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->tplBegin), 0);
        Free(kAkNsT8, e->t8);
        e->t8 = reinterpret_cast<uintptr_t>(Alloc(kAkNsT8, static_cast<size_t>(count) * kT8Size));
        e->tplCount = count;
        if (count && !e->t8) {
            return -1;
        }
    }

    int ext = 0;

    for (uint32_t t = 0; t < count; ++t) {
        const uint8_t* tpl = stock.get() + t * kTplSize;
        Col c[2];
        bool byName = false;

        ExtCols(b, tpl, t, v.n, c, &byName);
        uint8_t* out = reinterpret_cast<uint8_t*>(e->t8) + t * kT8Size;

        BuildT8(tpl, c, v.tun, v.n, v.capo, out);
        if (out[kT8Used] & 0xC0) {
            ++ext;
        }
        *fromName += byName;
    }

    MemoryBarrier();
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->tplBegin), static_cast<LONG>(v.tpl.begin));

    return ext;
}

// For a ChordNotes record Fixup appended (index >= origCn): the first note that uses it, its chord.
bool FindUser(const SongView& v, uint32_t r, uint32_t* noteMask, int32_t* chordId) {
    const size_t levels = (v.levels.end - v.levels.begin) / 100;

    for (size_t l = 0; l < levels; ++l) {
        Vec notes;
        if (!ReadVec(reinterpret_cast<const void*>(v.levels.begin + l * 100), 0x30, &notes)) {
            return false;
        }

        for (uintptr_t p = notes.begin; p + 0x1C8 <= notes.end; p += 0x1C8) {
            int32_t ids[2];
            uint32_t mask;
            if (!SafeCopy(ids, reinterpret_cast<const void*>(p + 0x14), 8) ||
                !SafeCopy(&mask, reinterpret_cast<const void*>(p), 4)) {
                return false;
            }

            if (ids[1] == static_cast<int32_t>(r) && ids[0] >= 0) {
                *noteMask = mask;
                *chordId = ids[0];
                return true;
            }
        }
    }

    return false;
}

void Log(const char* fmt, ...) {
    if (InterlockedIncrement(&g_logged) > 400) {
        return;
    }

    char buf[600];
    va_list a;

    va_start(a, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    Rs7Log("%s", buf);
}

// ChordNotes record r loaded with the song: the strings 6/7 technique of the RS7X block's CHNT section.
// Returns how many of the two carry a technique.
int BlockTech(const rs7ext::Block& block, uint32_t r, Tech* t, const Tech** tp) {
    int fromBlock = 0;

    for (int i = 0; i < 2; ++i) {
        const int c = block.Column(6 + i);
        const size_t at = static_cast<size_t>(r) * block.strings.size() + static_cast<size_t>(c);
        if (c < 0 || at >= block.tech.size()) {
            continue;
        }

        const rs7ext::Tech& x = block.tech[at];

        t[i].mask = x.mask;
        t[i].slideTo = x.slideTo;
        t[i].slideUnpitchTo = x.slideUnpitchTo;
        t[i].vibrato = x.vibrato;
        t[i].bends = x.bends.empty() ? nullptr : x.bends.data();
        t[i].bendCount = static_cast<int>(x.bends.size());
        tp[i] = &t[i];
        fromBlock += x.mask != 0;
    }

    return fromBlock;
}

// ChordNotes record r added by Fixup: the strings 6/7 technique synthesized from its user note and chord template.
// Returns how many were synthesized.
int SynthTech(const SongView& v, const Rs7NsEntry& e, uint32_t r, Tech* t, const Tech** tp) {
    uint32_t noteMask = 0;
    int32_t chord = -1;
    if (!FindUser(v, r, &noteMask, &chord) || chord < 0 || static_cast<uint32_t>(chord) >= e.tplCount) {
        return 0;
    }

    const int8_t* f = reinterpret_cast<const int8_t*>(e.t8 + chord * kT8Size + kT8Frets);
    int synth = 0;

    for (int i = 0; i < 2; ++i) {
        t[i].mask = SynthMask(noteMask, f[6 + i], v.capo);

        if (t[i].mask) {
            tp[i] = &t[i];
            ++synth;
        }
    }

    return synth;
}

// The outcome of FillC8Locked: on failure the scratch record was not allocated or record `bad` was unreadable.
struct C8Fill {
    bool ok = true, noScratch = false;
    uint32_t bad = 0;
    int fromBlock = 0, synth = 0;
};

// Builds C8 record by record from the song's ChordNotes and the strings 6/7 technique. Caller holds g_lock.
C8Fill FillC8Locked(Rs7NsEntry* e, const SongView& v, const rs7ext::Block* block, uint32_t count) {
    C8Fill out;
    std::unique_ptr<uint8_t[]> rec(new (std::nothrow) uint8_t[kCnSize]);

    if (!rec) {
        out.ok = false;
        out.noScratch = true;
        return out;
    }

    for (uint32_t r = 0; r < count; ++r) {
        if (!SafeCopy(rec.get(), reinterpret_cast<const void*>(v.cn.begin + r * kCnSize), kCnSize)) {
            out.ok = false;
            out.bad = r;
            return out;
        }

        Tech t[2];
        const Tech* tp[2] = {nullptr, nullptr};

        if (r < e->origCn && block && block->hasChordNotes) {
            out.fromBlock += BlockTech(*block, r, t, tp);
        } else if (r >= e->origCn) {
            out.synth += SynthTech(v, *e, r, t, tp);
        }

        BuildC8(rec.get(), tp[0], tp[1], reinterpret_cast<uint8_t*>(e->c8) + r * kC8Size);
    }

    return out;
}

}  // namespace

void Rs7Ns_OnSngLoaded(void* song) {
    try {
        if (!song) {
            return;
        }

        SongView v;

        if (!ReadSong(song, &v)) {
            AcquireSRWLockExclusive(&g_lock);
            if (auto* e = FindLocked(reinterpret_cast<uintptr_t>(song))) {
                ClearLocked(e);
            }
            ReleaseSRWLockExclusive(&g_lock);
            Log("nslot: SNG %p load refused: unreadable, partial, or oversized vectors; T8/C8 unpublished", song);
            return;
        }

        const auto block = Rs7Ext_Find(song);

        AcquireSRWLockExclusive(&g_lock);
        Rs7NsEntry* e = FindLocked(reinterpret_cast<uintptr_t>(song));

        if (e) {
            ClearLocked(e);  // a recycled song object
        }
        if (!e) {
            e = FindLocked(0);
        }

        if (!e) {
            ReleaseSRWLockExclusive(&g_lock);
            Log("nslot: SNG %p: no free registry slot (%d songs live) - its chords read past the stock records: "
                "REPORT THIS",
                song, kRs7NsEntries);
            return;
        }

        e->song = reinterpret_cast<uintptr_t>(song);
        e->origCn = static_cast<uint32_t>((v.cn.end - v.cn.begin) / kCnSize);
        int byName = 0;
        const int ext = BuildTemplates(e, v, block.get(), &byName);

        if (ext < 0) {
            DropArraysLocked(e);
        }
        ReleaseSRWLockExclusive(&g_lock);

        if (ext < 0) {
            Log("nslot: SNG %p load failed: template copy/allocation failed; T8 unpublished", song);
            return;
        }

        Log("nslot: SNG %p: %u templates -> T8 %p (%d with a 7th/8th-string column, from %s), %u tuning entries, "
            "capo %u, %u ChordNotes, tid %lu",
            song, e->tplCount, reinterpret_cast<void*>(e->t8), ext,
            block    ? "the RS7X block"
            : byName ? "name[31] (old format)"
                     : "nothing",
            v.n, v.capo, e->origCn,
            GetCurrentThreadId());  // L4 (review #3)
    } catch (...) {
    }
}

extern "C" void __cdecl Rs7Ns_OnPostProcessed(void* song) {
    try {
        if (!song) {
            return;
        }

        SongView v;

        if (!ReadSong(song, &v)) {
            AcquireSRWLockExclusive(&g_lock);
            if (auto* e = FindLocked(reinterpret_cast<uintptr_t>(song))) {
                DropArraysLocked(e);
            }
            ReleaseSRWLockExclusive(&g_lock);
            Log("nslot: SNG %p post-process refused: unreadable, partial, or oversized vectors; T8/C8 unpublished",
                song);
            return;
        }

        const auto block = Rs7Ext_Find(song);

        AcquireSRWLockExclusive(&g_lock);
        Rs7NsEntry* e = FindLocked(reinterpret_cast<uintptr_t>(song));

        if (!e) {
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }

        int byName = 0;
        const int ext = BuildTemplates(e, v, block.get(), &byName);  // PostProcess rewrote names / capo frets

        if (ext < 0) {
            DropArraysLocked(e);
            ReleaseSRWLockExclusive(&g_lock);
            Log("nslot: SNG %p post-process failed: template copy/allocation failed; T8/C8 unpublished", song);
            return;
        }

        const uint32_t count = static_cast<uint32_t>((v.cn.end - v.cn.begin) / kCnSize);

        InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->cnBegin), 0);
        Free(kAkNsC8, e->c8);
        e->c8 = reinterpret_cast<uintptr_t>(Alloc(kAkNsC8, static_cast<size_t>(count) * kC8Size));
        e->cnCount = count;

        if (count && !e->c8) {
            DropArraysLocked(e);
            ReleaseSRWLockExclusive(&g_lock);
            Log("nslot: SNG %p post-process failed: C8 allocation failed; T8/C8 unpublished", song);
            return;
        }

        C8Fill fill;

        if (count && e->c8) {
            fill = FillC8Locked(e, v, block.get(), count);

            if (!fill.ok) {
                // An incomplete array must never reach the lock-free redirect readers. Both arrays stay
                // unpublished; a later post-process can retry.
                DropArraysLocked(e);
                ReleaseSRWLockExclusive(&g_lock);
                if (fill.noScratch) {
                    Log("nslot: SNG %p post-process failed: scratch allocation failed; T8/C8 unpublished", song);
                } else {
                    Log("nslot: SNG %p post-process failed: ChordNotes record %u unreadable; T8/C8 unpublished", song,
                        fill.bad);
                }
                return;
            }

            MemoryBarrier();
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->cnBegin), static_cast<LONG>(v.cn.begin));
        }

        ReleaseSRWLockExclusive(&g_lock);
        Log("nslot: SNG %p post-processed: T8 re-synced (%d with an extended column), %u ChordNotes (%u loaded, %u "
            "added by Fixup) -> C8 %p (%d extended technique entries from the block, %d synthesized), tid %lu",
            song, ext, count, e->origCn, count > e->origCn ? count - e->origCn : 0, reinterpret_cast<void*>(e->c8),
            fill.fromBlock, fill.synth, GetCurrentThreadId());  // L4 (review #3)
    } catch (...) {
    }
}

extern "C" void __cdecl Rs7Ns_OnSongFree(void* song) {
    Rs7Diagram_Drop(song);

    try {
        if (!song) {
            return;
        }

        AcquireSRWLockExclusive(&g_lock);
        Rs7NsEntry* e = FindLocked(reinterpret_cast<uintptr_t>(song));

        if (e) {
            ClearLocked(e);
        }
        ReleaseSRWLockExclusive(&g_lock);
        // L5 (review #3): the thread that runs the SNG_Song destructor (SF1) - main (closed) or a loader (real).
        Log("nslot: SNG %p freed tid %lu%s", song, GetCurrentThreadId(), e ? " (had T8/C8)" : "");
    } catch (...) {
    }
}

const uint8_t* Rs7Ns_AsT8(const void* tpl) {
    const uintptr_t p = reinterpret_cast<uintptr_t>(tpl);

    for (const auto& e : g_rs7_ns) {
        if (!e.tplBegin || !e.t8) {
            continue;
        }
        if (p >= e.t8 && p < e.t8 + static_cast<uintptr_t>(e.tplCount) * kT8Size && (p - e.t8) % kT8Size == 0) {
            return reinterpret_cast<const uint8_t*>(p);
        }
    }

    return nullptr;
}

const uint8_t* Rs7Ns_AsC8(const void* cn) {
    const uintptr_t p = reinterpret_cast<uintptr_t>(cn);

    for (const auto& e : g_rs7_ns) {
        if (!e.cnBegin || !e.c8) {
            continue;
        }
        if (p >= e.c8 && p < e.c8 + static_cast<uintptr_t>(e.cnCount) * kC8Size && (p - e.c8) % kC8Size == 0) {
            return reinterpret_cast<const uint8_t*>(p);
        }
    }

    return nullptr;
}

uint32_t Rs7Ns_TplRecord(uint32_t begin, uint32_t id) {
    for (const auto& e : g_rs7_ns) {
        if (e.tplBegin == begin && id < e.tplCount) {
            return static_cast<uint32_t>(e.t8) + id * kT8Size;
        }
    }

    return begin + id * kTplSize;
}

uint32_t Rs7Ns_CnRecord(uint32_t begin, uint32_t id) {
    for (const auto& e : g_rs7_ns) {
        if (e.cnBegin == begin && id < e.cnCount) {
            return static_cast<uint32_t>(e.c8) + id * kC8Size;
        }
    }

    return begin + id * kCnSize;
}

int Rs7Ns_LiveSongs() {
    int n = 0;

    for (const auto& e : g_rs7_ns) {
        n += e.song != 0;
    }

    return n;
}

extern "C" void __cdecl Rs7Ns_OnEvalBuilt(uint8_t* eval, const uint8_t* tpl, const uint8_t* cn) {
    if (!eval) {
        return;
    }

    // Every template / ChordNotes pointer at play time is a T8 / C8 (TP*/CP*); anything else gets the
    // empty tails rather than bytes past a stock record.
    rs7ns::WriteEvalTails(eval, tpl ? Rs7Ns_AsT8(tpl) : nullptr, cn ? Rs7Ns_AsC8(cn) : nullptr);
}

extern "C" void __cdecl Rs7Ns_Shift8(int32_t* out8, const int32_t* arr6, const uint8_t* tpl, int shift) {
    std::memcpy(out8, arr6, 6 * 4);
    const uint8_t* t8 = tpl ? Rs7Ns_AsT8(tpl) : nullptr;

    for (int s = 6; s < 8; ++s) {
        int32_t m = -1;

        if (t8) {
            std::memcpy(&m, t8 + rs7ns::kT8Midi + 4 * s, 4);
        }
        out8[s] = m < 0 ? -1 : m + shift;
    }
}
