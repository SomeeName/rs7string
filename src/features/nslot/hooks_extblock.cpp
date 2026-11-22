// The extension block per loaded song (nslot.h): XB1 hands the SNG body's trailing bytes to the parser
// (model/extblock.h) and keeps the result for the song's T8 / C8 build (hooks.cpp), the chord diagrams and the leak
// audit.
#include "features/nslot/hooks.h"
#include "features/nslot/nslot.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstdio>
#include <utility>

namespace {

constexpr size_t kMaxTrailing = 16u << 20;
using rsxrnslot::kSongChordNotes;
using rsxrnslot::kSongTemplates;
using rsxrnslot::kSongTuning;
using rsxrnslot::ReadVec;
using rsxrnslot::Vec;

SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<std::pair<const void*, std::shared_ptr<const rsxrext::Block>>> g_store;  // newest last
volatile LONG g_logged = 0;

void Store(const void* song, std::shared_ptr<const rsxrext::Block> b) {
    AcquireSRWLockExclusive(&g_lock);

    for (size_t i = 0; i < g_store.size(); ++i) {
        if (g_store[i].first == song) {
            g_store.erase(g_store.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }

    if (b) {
        if (g_store.size() >= static_cast<size_t>(kRsxrExtKeep)) {
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

void OnLoaded(void* song, const void* stream) {
    const uint8_t* s = static_cast<const uint8_t*>(stream);
    uintptr_t buf = 0;
    uint32_t pos = 0, size = 0;

    Store(song, nullptr);  // a recycled song address never keeps the previous song's block
    if (!Rsxr_SafeCopy(&buf, s + 0x10, 4) || !Rsxr_SafeCopy(&pos, s + 0x14, 4) || !Rsxr_SafeCopy(&size, s + 0x18, 4)) {
        return;
    }

    Vec tv, cv, uv;
    if (!ReadVec(song, kSongTemplates, &tv) || !ReadVec(song, kSongChordNotes, &cv) ||
        !ReadVec(song, kSongTuning, &uv)) {
        return;
    }

    const uint32_t templates = static_cast<uint32_t>((tv.end - tv.begin) / rsxrns::kTplSize);
    const uint32_t chordNotes = static_cast<uint32_t>((cv.end - cv.begin) / rsxrns::kCnSize);
    const uint32_t tuning = static_cast<uint32_t>((uv.end - uv.begin) / 2);
    const bool log = RsxrLog_Upto(&g_logged, 200);

    if (pos > size || size - pos > kMaxTrailing) {
        if (log) {
            RsxrLog("extblock: SNG %p: stream position %u, size %u: not checked", song, pos, size);
        }
        return;
    }

    const size_t n = size - pos;
    std::vector<uint8_t> tail(n);
    if (n && !Rsxr_SafeCopy(tail.data(), reinterpret_cast<const void*>(buf + pos), n)) {
        return;
    }

    auto block = std::make_shared<rsxrext::Block>();
    char why[160];
    const rsxrext::Status st =
        rsxrext::Parse(tail.data(), n, templates, chordNotes, tuning, block.get(), why, sizeof why);

    if (st != rsxrext::Status::Ok) {
        char what[220];

        if (st == rsxrext::Status::None) {
            std::snprintf(what, sizeof what, "no extension block");
        } else if (st == rsxrext::Status::NotBlock) {
            std::snprintf(what, sizeof what, "trailing bytes, no extension block magic: ignored");
        } else {
            std::snprintf(what, sizeof what, "extension block %s (%s): ignored", rsxrext::StatusName(st), why);
        }

        if (log) {
            RsxrLog("extblock: SNG %p: body %u bytes, stock sections end at %u, %zu bytes after MetaData: %s -> "
                    "extra chord columns are empty (%u templates; %u tuning entries)",
                    song, size, pos, n, what, templates, tuning);
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
        RsxrLog("extblock: SNG %p: body %u bytes, stock sections end at %u, extension block v%u block %zu bytes: %s, "
                "%d strings, "
                "%d extended column(s), CHDT %s (%u templates, %d with a string-6 column), CHNT %s (%u records, %d "
                "extended technique entries), %d unknown section(s) skipped -> the N-slot chord data reads the block",
                song, size, pos, rsxrext::kVersion, n, block->instrument ? "bass" : "guitar", block->stringCount,
                block->Columns(), block->hasTemplates ? "yes" : "no", templates, ext6,
                block->hasChordNotes ? "yes" : "no", chordNotes, techs, block->skipped);
    }

    Store(song, std::move(block));
}

}  // namespace

extern "C" void __cdecl RsxrExt_OnSngLoaded(void* song, const void* stream) {
    try {
        if (song && stream) {
            OnLoaded(song, stream);
        }
        if (song) {
            RsxrNs_OnSngLoaded(song);  // the native N-slot templates (hooks.cpp)
        }
    } catch (...) {
        // A recycled song must not retain native arrays when the load failed before
        // RsxrNs_OnSngLoaded could invalidate them. Cleanup/logging must not escape either.
        try {
            if (song) {
                RsxrNs_OnSongFree(song);
            }
            RsxrLog("extblock: SNG %p load failed: exception; T8/C8 unpublished", song);
        } catch (...) {
            // Never let an exception cross into the loader's frames.
        }
    }
}

void RsxrExt_Store(const void* song, std::shared_ptr<const rsxrext::Block> b) {
    Store(song, std::move(b));
}

int RsxrExt_StoredBlocks() {
    AcquireSRWLockShared(&g_lock);
    const int n = int(g_store.size());

    ReleaseSRWLockShared(&g_lock);

    return n;
}

std::shared_ptr<const rsxrext::Block> RsxrExt_Find(const void* song) {
    std::shared_ptr<const rsxrext::Block> b;

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
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_XB1(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    RsxrExt_OnSngLoaded(reinterpret_cast<void*>(RsxrMid::Dword(c.ebp + 8)),
                        reinterpret_cast<const void*>(c.esp + 0x14));
}
