#include "core/nslot.h"
#include "core/allocstats.h"
#include "core/diagrams.h"
#include "core/eight.h"
#include "core/extblock.h"
#include "core/publish.h"
#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace rs7ns {

bool DecodeName31(const uint8_t* tpl, Col* out) {
    const uint8_t* name = tpl + 0x28;
    if (!std::memchr(name, 0, 31)) {
        return false;  // a 31-character name: name[31] is text
    }

    const uint8_t b = name[31];
    const int fret = (b & 0x1F) - 1, finger = (b >> 5) - 1;
    if (fret < 0) {
        return false;
    }

    out->fret = fret;
    out->finger = finger;

    return true;
}

void BuildT8(const uint8_t* tpl, const Col ext[2], const int16_t* tuning, int n, uint8_t capo, uint8_t* out) {
    std::memset(out, 0, kT8Size);
    std::memcpy(out, tpl, kTplSize);
    int8_t* frets = reinterpret_cast<int8_t*>(out + kT8Frets);
    int8_t* fingers = reinterpret_cast<int8_t*>(out + kT8Fingers);
    int32_t* midi = reinterpret_cast<int32_t*>(out + kT8Midi);

    for (int s = 0; s < kSlots; ++s) {
        frets[s] = -1;
        fingers[s] = -1;
        midi[s] = -1;
    }

    for (int s = 0; s < 6; ++s) {
        frets[s] = static_cast<int8_t>(tpl[4 + s]);
        fingers[s] = static_cast<int8_t>(tpl[0xA + s]);
        std::memcpy(&midi[s], tpl + 0x10 + 4 * s, 4);
    }

    for (int i = 0; i < 2; ++i) {
        const int s = 6 + i;
        int f = ext[i].fret;
        // Gameplay is still seven-wide; diagrams also need the authored eighth
        // column. Populate it with diagrams or the offline eight preparation switch.
        if (f < 0 || f > 30 || s >= n ||
            (n > kNative && !((g_rs7_diagrams || g_rs7_publish_ports || g_rs7_eight) && n == kSlots))) {
            continue;
        }

        // MIDI from the authored fret (stock template midi[] keeps it too); the fret then follows
        // PostProcessNotes 0x55B4A2, which turns a fret equal to the capo into 0 (frets only).
        midi[s] = kOpenMidi[s] + (tuning && s < n ? tuning[s] : 0) + f;
        if (static_cast<uint8_t>(f) == capo) {
            f = 0;
        }
        frets[s] = static_cast<int8_t>(f);
        // Fingers 0..4 only: the neck finger keep table 0x58C300 has five entries (thumb..pinky).
        fingers[s] = static_cast<int8_t>(ext[i].finger >= -1 && ext[i].finger <= 4 ? ext[i].finger : -1);
    }

    out[kT8Slots] = static_cast<uint8_t>(n > 6 && n <= kSlots ? n : 6);
    uint8_t used = 0;

    for (int s = 0; s < kSlots; ++s) {
        if (frets[s] != -1) {
            used = static_cast<uint8_t>(used | (1u << s));
        }
    }

    out[kT8Used] = used;
}

void BuildC8(const uint8_t* cn, const Tech* tech6, const Tech* tech7, uint8_t* out) {
    std::memset(out, 0, kC8Size);
    std::memcpy(out, cn, kCnSize);
    uint32_t* mask = reinterpret_cast<uint32_t*>(out + kC8Mask);
    int8_t* slide = reinterpret_cast<int8_t*>(out + kC8SlideTo);
    int8_t* unp = reinterpret_cast<int8_t*>(out + kC8SlideUnp);
    int16_t* vib = reinterpret_cast<int16_t*>(out + kC8Vib);

    for (int s = 0; s < kSlots; ++s) {
        slide[s] = -1;
        unp[s] = -1;
    }

    for (int s = 0; s < 6; ++s) {
        std::memcpy(&mask[s], cn + 4 * s, 4);
        slide[s] = static_cast<int8_t>(cn[0x930 + s]);
        unp[s] = static_cast<int8_t>(cn[0x936 + s]);
        std::memcpy(&vib[s], cn + 0x93C + 2 * s, 2);
        std::memcpy(out + kC8Bends + s * kBendSize, cn + 0x18 + s * kBendSize, kBendSize);
    }

    const Tech* tech[2] = {tech6, tech7};

    for (int i = 0; i < 2; ++i) {
        const Tech* t = tech[i];
        if (!t) {
            continue;
        }

        const int s = 6 + i;
        uint32_t m = t->mask;
        int8_t to = t->slideTo, un = t->slideUnpitchTo;

        // SNG_Level_FixupChordNotes 0x55D490: an unpitched slide flagged 0x8000000 without a pitched
        // one becomes a pitched slide (strings 0-5 get this in the stock record).
        if ((m & 0x8000000) && un != -1 && to == -1) {
            to = un;
            un = -1;
            m = (m & ~0x400000u) | 0x800u;
        }

        mask[s] = m;
        slide[s] = to;
        unp[s] = un;
        vib[s] = t->vibrato;
        uint8_t* b = out + kC8Bends + s * kBendSize;
        const int count = t->bendCount < 0 ? 0 : t->bendCount > 32 ? 32 : t->bendCount;

        if (count && t->bends) {
            std::memcpy(b, t->bends, static_cast<size_t>(count) * 12);
        }
        const uint32_t used = static_cast<uint32_t>(count);

        std::memcpy(b + 0x180, &used, 4);
    }
}

uint32_t SynthMask(uint32_t noteMask, int fret, uint8_t capo) {
    if (fret < 0) {
        return 0;
    }

    uint32_t m = 0;

    if (noteMask & 0x40) {
        m |= 0x40;
    }
    if (noteMask & 8) {
        m |= 0x20000;
    }
    if (noteMask & 0x4000000) {
        m |= 0x4000000;
    }
    if ((fret == capo && capo != 0xFF) || fret == 0) {
        m |= 4;
    }

    return m;
}

}  // namespace rs7ns

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

        int fromBlock = 0, synth = 0;

        if (count && e->c8) {
            std::unique_ptr<uint8_t[]> rec(new (std::nothrow) uint8_t[kCnSize]);

            if (!rec) {
                DropArraysLocked(e);
                ReleaseSRWLockExclusive(&g_lock);
                Log("nslot: SNG %p post-process failed: scratch allocation failed; T8/C8 unpublished", song);
                return;  // Both arrays stay unpublished; a later post-process can retry.
            }

            for (uint32_t r = 0; r < count; ++r) {
                if (!SafeCopy(rec.get(), reinterpret_cast<const void*>(v.cn.begin + r * kCnSize), kCnSize)) {
                    // An incomplete array must never reach the lock-free redirect readers.
                    DropArraysLocked(e);
                    ReleaseSRWLockExclusive(&g_lock);
                    Log("nslot: SNG %p post-process failed: ChordNotes record %u unreadable; T8/C8 unpublished", song,
                        r);
                    return;
                }

                Tech t[2];
                const Tech* tp[2] = {nullptr, nullptr};

                if (r < e->origCn && block && block->hasChordNotes) {
                    for (int i = 0; i < 2; ++i) {
                        const int c = block->Column(6 + i);
                        const size_t at = static_cast<size_t>(r) * block->strings.size() + static_cast<size_t>(c);
                        if (c < 0 || at >= block->tech.size()) {
                            continue;
                        }

                        const rs7ext::Tech& x = block->tech[at];

                        t[i].mask = x.mask;
                        t[i].slideTo = x.slideTo;
                        t[i].slideUnpitchTo = x.slideUnpitchTo;
                        t[i].vibrato = x.vibrato;
                        t[i].bends = x.bends.empty() ? nullptr : x.bends.data();
                        t[i].bendCount = static_cast<int>(x.bends.size());
                        tp[i] = &t[i];
                        fromBlock += x.mask != 0;
                    }
                } else if (r >= e->origCn) {
                    uint32_t noteMask = 0;
                    int32_t chord = -1;

                    if (FindUser(v, r, &noteMask, &chord) && chord >= 0 && static_cast<uint32_t>(chord) < e->tplCount) {
                        const int8_t* f = reinterpret_cast<const int8_t*>(e->t8 + chord * kT8Size + kT8Frets);

                        for (int i = 0; i < 2; ++i) {
                            t[i].mask = SynthMask(noteMask, f[6 + i], v.capo);

                            if (t[i].mask) {
                                tp[i] = &t[i];
                                ++synth;
                            }
                        }
                    }
                }

                BuildC8(rec.get(), tp[0], tp[1], reinterpret_cast<uint8_t*>(e->c8) + r * kC8Size);
            }

            MemoryBarrier();
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&e->cnBegin), static_cast<LONG>(v.cn.begin));
        }

        ReleaseSRWLockExclusive(&g_lock);
        Log("nslot: SNG %p post-processed: T8 re-synced (%d with an extended column), %u ChordNotes (%u loaded, %u "
            "added by Fixup) -> C8 %p (%d extended technique entries from the block, %d synthesized), tid %lu",
            song, ext, count, e->origCn, count > e->origCn ? count - e->origCn : 0, reinterpret_cast<void*>(e->c8),
            fromBlock, synth, GetCurrentThreadId());  // L4 (review #3)
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

int Rs7Ns_LiveSongs() {
    int n = 0;

    for (const auto& e : g_rs7_ns) {
        n += e.song != 0;
    }

    return n;
}

namespace rs7ns {

void ClearEvalTails(uint8_t* ev) {
    const int32_t none[2] = {-1, -1};

    std::memcpy(ev + kEvMidi67, none, 8);
    std::memset(ev + kEvMask67, 0, 8);
    std::memset(ev + kEvFret67, 0xFF, 8);  // frets, fingers, slideTo, slideUnpitchTo
    std::memset(ev + kEvVib67, 0, 8);      // vibrato[2] + the last 4 name bytes
}

void WriteEvalTails(uint8_t* ev, const uint8_t* t8, const uint8_t* c8) {
    ClearEvalTails(ev);

    if (t8) {
        std::memcpy(ev + kEvMidi67, t8 + kT8Midi + 6 * 4, 8);
        std::memcpy(ev + kEvFret67, t8 + kT8Frets + 6, 2);
        std::memcpy(ev + kEvFinger67, t8 + kT8Fingers + 6, 2);
    }

    if (c8) {
        std::memcpy(ev + kEvMask67, c8 + kC8Mask + 6 * 4, 8);
        std::memcpy(ev + kEvSlide67, c8 + kC8SlideTo + 6, 2);
        std::memcpy(ev + kEvUnp67, c8 + kC8SlideUnp + 6, 2);
        std::memcpy(ev + kEvVib67, c8 + kC8Vib + 6 * 2, 4);
    }
}

}  // namespace rs7ns

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
