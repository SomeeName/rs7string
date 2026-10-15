#include "core/extblock.h"
#include "core/chord7.h"
#include "core/nslot.h"
#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

namespace rs7ext {

int Block::Column(int string) const {
    for (size_t i = 0; i < strings.size(); ++i) {
        if (strings[i] == string) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool Block::TemplateColumn(uint32_t t, int string, int* f, int* g) const {
    const int c = Column(string);
    if (c < 0 || !hasTemplates || strings.empty() || t >= fret.size() / strings.size()) {
        return false;
    }

    const size_t i = static_cast<size_t>(t) * strings.size() + static_cast<size_t>(c);
    if (i >= fret.size() || i >= finger.size() || fret[i] < 0) {
        return false;
    }

    *f = fret[i];
    *g = finger[i];

    return true;
}

const char* StatusName(Status s) {
    switch (s) {
        case Status::None:
            return "none";
        case Status::Ok:
            return "ok";
        case Status::NotBlock:
            return "not an RS7X block";
        case Status::Unsupported:
            return "unsupported";
        case Status::Invalid:
            return "INVALID";
    }

    return "?";
}

namespace {

struct Cursor {
    const uint8_t* p;
    size_t n;
    size_t at = 0;

    bool Take(void* out, size_t k) {
        if (k > n - at) {
            return false;
        }

        std::memcpy(out, p + at, k);
        at += k;

        return true;
    }

    template <class T>
    bool Get(T* v) {
        return Take(v, sizeof(T));
    }
};

Status Fail(Status s, char* why, size_t len, const char* fmt, ...) {
    if (why && len) {
        va_list a;

        va_start(a, fmt);
        std::vsnprintf(why, len, fmt, a);
        va_end(a);
    }

    return s;
}

bool Id(const uint8_t* id, const char* s) {
    return std::memcmp(id, s, 4) == 0;
}

}  // namespace

Status Parse(const uint8_t* p, size_t n, uint32_t templates, uint32_t chordNotes, uint32_t tuning, Block* out,
             char* why, size_t whyLen) {
    if (why && whyLen) {
        why[0] = 0;
    }
    if (n == 0) {
        return Status::None;
    }
    if (!p || !out) {
        return Fail(Status::Invalid, why, whyLen, "null input or output for nonempty block");
    }
    if (n < 4 || std::memcmp(p, "RS7X", 4) != 0) {
        return Fail(Status::NotBlock, why, whyLen, "no RS7X magic");
    }
    if (n < kHeaderSize) {
        return Fail(Status::Invalid, why, whyLen, "%zu bytes, shorter than the header", n);
    }

    uint16_t version, hsize;
    uint32_t bsize, count;

    std::memcpy(&version, p + 4, 2);
    std::memcpy(&hsize, p + 6, 2);
    std::memcpy(&bsize, p + 8, 4);
    std::memcpy(&count, p + 12, 4);
    if (version != kVersion) {
        return Fail(Status::Unsupported, why, whyLen, "version %u", version);
    }
    if (bsize != n || hsize < kHeaderSize || hsize > bsize) {
        return Fail(Status::Invalid, why, whyLen, "header size %u, block size %u, %zu bytes after MetaData", hsize,
                    bsize, n);
    }

    *out = Block();
    size_t at = hsize;
    bool strs = false, chdt = false, chnt = false;

    for (uint32_t s = 0; s < count; ++s) {
        if (bsize - at < kSectionHeader) {
            return Fail(Status::Invalid, why, whyLen, "section %u: header past the end", s);
        }

        const uint8_t* id = p + at;
        uint16_t ver, flags;
        uint32_t size;

        std::memcpy(&ver, p + at + 4, 2);
        std::memcpy(&flags, p + at + 6, 2);
        std::memcpy(&size, p + at + 8, 4);
        at += kSectionHeader;
        if (size > bsize - at) {
            return Fail(Status::Invalid, why, whyLen, "section %u: %u bytes past the end", s, size);
        }

        Cursor c{p + at, size};

        at += size;
        const bool isStrs = Id(id, "STRS"), isChdt = Id(id, "CHDT"), isChnt = Id(id, "CHNT");
        if (s == 0 && !isStrs) {
            return Fail(Status::Invalid, why, whyLen, "the first section is not STRS");
        }
        if ((isStrs && strs) || (isChdt && chdt) || (isChnt && chnt)) {
            return Fail(Status::Invalid, why, whyLen, "section %.4s twice", reinterpret_cast<const char*>(id));
        }

        if ((!isStrs && !isChdt && !isChnt) || ver != 1) {
            if (flags & kFlagCritical) {
                return Fail(Status::Unsupported, why, whyLen, "critical section %.4s v%u",
                            reinterpret_cast<const char*>(id), ver);
            }

            ++out->skipped;
            continue;
        }

        if (isStrs) {
            strs = true;
            uint8_t h[4];
            if (!c.Take(h, 4)) {
                return Fail(Status::Invalid, why, whyLen, "STRS too short");
            }

            out->instrument = h[0];
            out->stringCount = h[1];
            if (out->instrument != 0) {
                return Fail(Status::Unsupported, why, whyLen, "instrument %d", out->instrument);
            }
            if (out->stringCount < 7 || out->stringCount > kMaxStrings) {
                return Fail(Status::Invalid, why, whyLen, "%d strings", out->stringCount);
            }
            if (static_cast<uint32_t>(out->stringCount) != tuning) {
                return Fail(Status::Invalid, why, whyLen, "%d strings but %u tuning entries", out->stringCount, tuning);
            }
            if (h[2] != out->stringCount - 6) {
                return Fail(Status::Invalid, why, whyLen, "%u columns for %d strings", h[2], out->stringCount);
            }

            for (int i = 0; i < h[2]; ++i) {
                uint8_t st;
                if (!c.Get(&st)) {
                    return Fail(Status::Invalid, why, whyLen, "STRS too short");
                }
                if (st != 6 + i) {
                    return Fail(Status::Invalid, why, whyLen, "column %d is string %u", i, st);
                }

                out->strings.push_back(st);
            }
        } else if (isChdt) {
            chdt = out->hasTemplates = true;
            uint32_t rows;
            if (!c.Get(&rows)) {
                return Fail(Status::Invalid, why, whyLen, "CHDT too short");
            }
            if (rows != templates) {
                return Fail(Status::Invalid, why, whyLen, "CHDT has %u templates, the SNG %u", rows, templates);
            }

            // Validate by division before multiplying: size_t is 32-bit in the game.
            const size_t rowBytes = out->strings.size() * 2;
            const size_t remaining = c.n - c.at;
            if (!rowBytes || remaining % rowBytes || rows != remaining / rowBytes) {
                return Fail(Status::Invalid, why, whyLen, "CHDT size");
            }

            const size_t cells = static_cast<size_t>(rows) * out->strings.size();

            out->fret.resize(cells);
            out->finger.resize(cells);

            for (size_t i = 0; i < cells; ++i) {
                int8_t f, g;

                c.Get(&f);
                c.Get(&g);
                if (!(f == -1 || (f >= 0 && f <= kMaxFret)) || g < -1 || g > 4 || (f == -1 && g != -1)) {
                    return Fail(Status::Invalid, why, whyLen, "CHDT template %zu: fret %d finger %d",
                                i / out->strings.size(), f, g);
                }

                out->fret[i] = f;
                out->finger[i] = g;
            }
        } else {
            chnt = out->hasChordNotes = true;
            uint32_t rows;
            if (!c.Get(&rows)) {
                return Fail(Status::Invalid, why, whyLen, "CHNT too short");
            }
            if (rows != chordNotes) {
                return Fail(Status::Invalid, why, whyLen, "CHNT has %u records, the SNG %u", rows, chordNotes);
            }

            // Every cell has nine fixed bytes, even with no bends. Bound both the wire
            // count and the in-memory Tech array before any product or allocation.
            const size_t columns = out->strings.size();
            if (!columns || rows > (c.n - c.at) / (columns * 9) || rows > SIZE_MAX / sizeof(Tech) / columns) {
                return Fail(Status::Invalid, why, whyLen, "CHNT size");
            }

            const size_t cells = static_cast<size_t>(rows) * columns;

            out->tech.resize(cells);

            for (size_t i = 0; i < cells; ++i) {
                Tech& t = out->tech[i];
                uint8_t bends;
                if (!c.Get(&t.mask) || !c.Get(&t.slideTo) || !c.Get(&t.slideUnpitchTo) || !c.Get(&t.vibrato) ||
                    !c.Get(&bends)) {
                    return Fail(Status::Invalid, why, whyLen, "CHNT too short");
                }
                if (bends > kMaxBends) {
                    return Fail(Status::Invalid, why, whyLen, "CHNT %u bends", bends);
                }
                if (!(t.slideTo == -1 || (t.slideTo >= 0 && t.slideTo <= kMaxFret)) ||
                    !(t.slideUnpitchTo == -1 || (t.slideUnpitchTo >= 0 && t.slideUnpitchTo <= kMaxFret))) {
                    return Fail(Status::Invalid, why, whyLen, "CHNT record %zu: slide %d / %d", i / out->strings.size(),
                                t.slideTo, t.slideUnpitchTo);
                }

                t.bends.resize(bends);

                for (Bend& b : t.bends) {
                    if (!c.Get(&b)) {
                        return Fail(Status::Invalid, why, whyLen, "CHNT too short");
                    }
                }
            }
        }

        if (c.at != c.n) {
            return Fail(Status::Invalid, why, whyLen, "section %.4s: %zu unread bytes",
                        reinterpret_cast<const char*>(id), c.n - c.at);
        }
    }

    if (at != bsize) {
        return Fail(Status::Invalid, why, whyLen, "%zu bytes after the last section", bsize - at);
    }
    if (!strs) {
        return Fail(Status::Invalid, why, whyLen, "no STRS section");
    }

    return Status::Ok;
}

}  // namespace rs7ext

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
            Rs7Ns_OnSngLoaded(song);  // the native N-slot templates (core/nslot.h)
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
