// The RS7X block per loaded song (nslot.h): XB1 hands the SNG body's trailing bytes to the parser (model/extblock.h)
// and keeps the result for the song's T8 / C8 build (hooks.cpp), the chord diagrams and the leak audit.
#include "features/chords/chords.h"
#include "features/nslot/nslot.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

constexpr size_t kMaxTrailing = 16u << 20;
constexpr uint32_t kSongTemplates = 0x94, kSongChordNotes = 0xAC, kSongTuning = 0x110;
constexpr uint32_t kChordNotesSize = 0x948;

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

SRWLOCK g_lock = SRWLOCK_INIT;
constexpr size_t kKeep = kRs7ExtKeep;
std::vector<std::pair<const void*, std::shared_ptr<const rs7ext::Block>>> g_store;  // newest last
volatile LONG g_logged = 0;

void Store(const void* song, std::shared_ptr<const rs7ext::Block> b) {
    AcquireSRWLockExclusive(&g_lock);

    for (size_t i = 0; i < g_store.size(); ++i) {
        if (g_store[i].first == song) {
            g_store.erase(g_store.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }

    if (b) {
        if (g_store.size() >= kKeep) {
            g_store.erase(g_store.begin());
        }

        try {
            g_store.emplace_back(song, std::move(b));
        } catch (...) {
            // Allocation failure drops this block; release the lock before returning to the loader.
        }
    }

    ReleaseSRWLockExclusive(&g_lock);
}

int CountByte31(const std::vector<uint8_t>& tpls) {
    int n = 0;
    rs7chord::Ext x;

    for (size_t at = 0; at + rs7chord::kTplSize <= tpls.size(); at += rs7chord::kTplSize) {
        n += rs7chord::FromTemplateBytes(tpls.data() + at, &x) ? 1 : 0;
    }

    return n;
}

void OnLoaded(void* song, const void* stream) {
    const uint8_t* s = static_cast<const uint8_t*>(stream);
    uintptr_t buf = 0;
    uint32_t pos = 0, size = 0;

    Store(song, nullptr);  // a recycled song address never keeps the previous song's block
    if (!SafeCopy(&buf, s + 0x10, 4) || !SafeCopy(&pos, s + 0x14, 4) || !SafeCopy(&size, s + 0x18, 4)) {
        return;
    }

    Vec tv, cv, uv;
    if (!ReadVec(song, kSongTemplates, &tv) || !ReadVec(song, kSongChordNotes, &cv) ||
        !ReadVec(song, kSongTuning, &uv)) {
        return;
    }

    const uint32_t templates = static_cast<uint32_t>((tv.end - tv.begin) / rs7chord::kTplSize);
    const uint32_t chordNotes = static_cast<uint32_t>((cv.end - cv.begin) / kChordNotesSize);
    const uint32_t tuning = static_cast<uint32_t>((uv.end - uv.begin) / 2);
    std::vector<uint8_t> tpls(static_cast<size_t>(templates) * rs7chord::kTplSize);
    if (!tpls.empty() && !SafeCopy(tpls.data(), reinterpret_cast<const void*>(tv.begin), tpls.size())) {
        return;
    }

    const bool log = InterlockedIncrement(&g_logged) <= 200;

    if (pos > size || size - pos > kMaxTrailing) {
        if (log) {
            Rs7Log("extblock: SNG %p: stream position %u, size %u: not checked", song, pos, size);
        }
        Store(song, nullptr);
        return;
    }

    const size_t n = size - pos;
    std::vector<uint8_t> tail(n);
    if (n && !SafeCopy(tail.data(), reinterpret_cast<const void*>(buf + pos), n)) {
        return;
    }

    auto block = std::make_shared<rs7ext::Block>();
    char why[160];
    const rs7ext::Status st =
        rs7ext::Parse(tail.data(), n, templates, chordNotes, tuning, block.get(), why, sizeof why);

    if (st != rs7ext::Status::Ok) {
        Store(song, nullptr);
        char what[220];

        if (st == rs7ext::Status::None) {
            std::snprintf(what, sizeof what, "no RS7X block");
        } else if (st == rs7ext::Status::NotBlock) {
            std::snprintf(what, sizeof what, "trailing bytes, no RS7X magic: ignored");
        } else {
            std::snprintf(what, sizeof what, "RS7X block %s (%s): ignored", rs7ext::StatusName(st), why);
        }

        if (log) {
            Rs7Log(
                "extblock: SNG %p: body %u bytes, stock sections end at %u, %zu bytes after MetaData: %s -> name[31] "
                "fallback (%d of %u templates carry a string-6 column there; %u tuning entries)",
                song, size, pos, n, what, CountByte31(tpls), templates, tuning);
        }

        return;
    }

    int ext6 = 0;

    for (uint32_t tp = 0; tp < templates; ++tp) {
        int f, g;

        ext6 += block->TemplateColumn(tp, 6, &f, &g) ? 1 : 0;
    }

    int techs = 0;

    for (const auto& t : block->tech) {
        techs += t.mask || t.slideTo != -1 || t.slideUnpitchTo != -1 || t.vibrato || !t.bends.empty();
    }

    if (log) {
        Rs7Log(
            "extblock: SNG %p: body %u bytes, stock sections end at %u, RS7X v%u block %zu bytes: guitar, %d strings, "
            "%d extended column(s), CHDT %s (%u templates, %d with a string-6 column), CHNT %s (%u records, %d "
            "extended technique entries), %d unknown section(s) skipped -> the N-slot chord data reads the block",
            song, size, pos, rs7ext::kVersion, n, block->stringCount, block->Columns(),
            block->hasTemplates ? "yes" : "no", templates, ext6, block->hasChordNotes ? "yes" : "no", chordNotes, techs,
            block->skipped);
    }

    Store(song, std::move(block));
}

}  // namespace

extern "C" void __cdecl Rs7Ext_OnSngLoaded(void* song, const void* stream) {
    try {
        if (song && stream) {
            OnLoaded(song, stream);
        }
        if (song) {
            Rs7Ns_OnSngLoaded(song);  // the native N-slot templates (hooks.cpp)
        }
    } catch (...) {
        // A recycled song must not retain native arrays when the load failed before
        // Rs7Ns_OnSngLoaded could invalidate them. Cleanup/logging must not escape either.
        try {
            if (song) {
                Rs7Ns_OnSongFree(song);
            }
            Rs7Log("extblock: SNG %p load failed: exception; T8/C8 unpublished", song);
        } catch (...) {
            // Never let an exception cross into the loader's frames.
        }
    }
}

void Rs7Ext_Store(const void* song, std::shared_ptr<const rs7ext::Block> b) {
    Store(song, std::move(b));
}

int Rs7Ext_StoredBlocks() {
    AcquireSRWLockShared(&g_lock);
    const int n = int(g_store.size());

    ReleaseSRWLockShared(&g_lock);

    return n;
}

std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void* song) {
    std::shared_ptr<const rs7ext::Block> b;

    AcquireSRWLockShared(&g_lock);

    for (const auto& e : g_store) {
        if (e.first == song) {
            b = e.second;
        }
    }

    ReleaseSRWLockShared(&g_lock);

    return b;
}

// XB1 - 0x55922C at the end of SNG_Song_Load 0x558BD0 (loader thread), after the last MetaData read and
// before the body's memory stream is destroyed: song = [EBP+8], stream = the site's ESP + 14h. A C++ mid-hook
// callback (equivalence against the frozen stub: tools/tests/test_midhook_cold.py); the two stock instructions
// (`lea ecx,[esp+14h]; call 0x9D8B10`) are replayed by the thunk. Must not throw.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_XB1(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Ext_OnSngLoaded(reinterpret_cast<void*>(Rs7Mid::Dword(c.ebp + 8)), reinterpret_cast<const void*>(c.esp + 0x14));
}
