#include "model/extblock.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

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

// The song counts a block is checked against, the block being filled and where a failure's reason goes.
struct Context {
    uint32_t templates, chordNotes, tuning;
    Block* out;
    char* why;
    size_t whyLen;
};

struct Header {
    uint16_t size;
    uint32_t blockSize, count;
};

// The sections met so far: each may appear once.
struct Seen {
    size_t first;
    uint32_t ids[16] = {};
    size_t count = 0;

    bool Add(uint32_t id, const uint8_t* p, size_t current) {
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] == id) {
                return false;
            }
        }

        if (count < sizeof ids / sizeof ids[0]) {
            ids[count++] = id;
            return true;
        }

        // Optional future sections have no format-level count limit. Beyond the small cache, scan only
        // headers already validated by ParseSection rather than allocating or imposing a new wire limit.
        for (size_t at = first; at < current;) {
            uint32_t previous, size;

            std::memcpy(&previous, p + at, sizeof previous);
            std::memcpy(&size, p + at + 8, sizeof size);
            if (previous == id) {
                return false;
            }

            at += kSectionHeader + size_t(size);
        }

        return true;
    }
};

Status ReadHeader(const uint8_t* p, size_t n, Header* h, char* why, size_t whyLen) {
    if (n < 4 || std::memcmp(p, "RS7X", 4) != 0) {
        return Fail(Status::NotBlock, why, whyLen, "no RS7X magic");
    }
    if (n < kHeaderSize) {
        return Fail(Status::Invalid, why, whyLen, "%zu bytes, shorter than the header", n);
    }

    uint16_t version;

    std::memcpy(&version, p + 4, 2);
    std::memcpy(&h->size, p + 6, 2);
    std::memcpy(&h->blockSize, p + 8, 4);
    std::memcpy(&h->count, p + 12, 4);
    if (version != kVersion) {
        return Fail(Status::Unsupported, why, whyLen, "version %u", version);
    }
    if (h->blockSize != n || h->size < kHeaderSize || h->size > h->blockSize) {
        return Fail(Status::Invalid, why, whyLen, "header size %u, block size %u, %zu bytes after MetaData", h->size,
                    h->blockSize, n);
    }

    return Status::Ok;
}

Status ParseStrs(const Context& x, Cursor& c) {
    Block* out = x.out;
    uint8_t h[4];
    if (!c.Take(h, 4)) {
        return Fail(Status::Invalid, x.why, x.whyLen, "STRS too short");
    }

    out->instrument = h[0];
    out->stringCount = h[1];
    if (out->instrument != 0) {
        return Fail(Status::Unsupported, x.why, x.whyLen, "instrument %d", out->instrument);
    }
    if (out->stringCount < 7 || out->stringCount > kMaxStrings) {
        return Fail(Status::Invalid, x.why, x.whyLen, "%d strings", out->stringCount);
    }
    if (static_cast<uint32_t>(out->stringCount) != x.tuning) {
        return Fail(Status::Invalid, x.why, x.whyLen, "%d strings but %u tuning entries", out->stringCount, x.tuning);
    }
    if (h[2] != out->stringCount - 6) {
        return Fail(Status::Invalid, x.why, x.whyLen, "%u columns for %d strings", h[2], out->stringCount);
    }

    for (int i = 0; i < h[2]; ++i) {
        uint8_t st;
        if (!c.Get(&st)) {
            return Fail(Status::Invalid, x.why, x.whyLen, "STRS too short");
        }
        if (st != 6 + i) {
            return Fail(Status::Invalid, x.why, x.whyLen, "column %d is string %u", i, st);
        }

        out->strings.push_back(st);
    }

    return Status::Ok;
}

Status ParseChdt(const Context& x, Cursor& c) {
    Block* out = x.out;
    uint32_t rows;
    if (!c.Get(&rows)) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHDT too short");
    }
    if (rows != x.templates) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHDT has %u templates, the SNG %u", rows, x.templates);
    }

    // Validate by division before multiplying: size_t is 32-bit in the game.
    const size_t rowBytes = out->strings.size() * 2;
    const size_t remaining = c.n - c.at;
    if (!rowBytes || remaining % rowBytes || rows != remaining / rowBytes) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHDT size");
    }

    const size_t cells = static_cast<size_t>(rows) * out->strings.size();

    out->fret.resize(cells);
    out->finger.resize(cells);

    for (size_t i = 0; i < cells; ++i) {
        int8_t f, g;

        c.Get(&f);
        c.Get(&g);
        if (!(f == -1 || (f >= 0 && f <= kMaxFret)) || g < -1 || g > 4 || (f == -1 && g != -1)) {
            return Fail(Status::Invalid, x.why, x.whyLen, "CHDT template %zu: fret %d finger %d",
                        i / out->strings.size(), f, g);
        }

        out->fret[i] = f;
        out->finger[i] = g;
    }

    return Status::Ok;
}

// One CHNT cell: mask, slides, vibrato and its bends. `record` is the ChordNotes record it belongs to.
Status ParseTech(const Context& x, Cursor& c, size_t record, Tech& t) {
    uint8_t bends;
    if (!c.Get(&t.mask) || !c.Get(&t.slideTo) || !c.Get(&t.slideUnpitchTo) || !c.Get(&t.vibrato) || !c.Get(&bends)) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT too short");
    }
    if (bends > kMaxBends) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT %u bends", bends);
    }
    if (!(t.slideTo == -1 || (t.slideTo >= 0 && t.slideTo <= kMaxFret)) ||
        !(t.slideUnpitchTo == -1 || (t.slideUnpitchTo >= 0 && t.slideUnpitchTo <= kMaxFret))) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT record %zu: slide %d / %d", record, t.slideTo,
                    t.slideUnpitchTo);
    }

    t.bends.resize(bends);

    for (Bend& b : t.bends) {
        if (!c.Get(&b)) {
            return Fail(Status::Invalid, x.why, x.whyLen, "CHNT too short");
        }
    }

    return Status::Ok;
}

Status ParseChnt(const Context& x, Cursor& c) {
    Block* out = x.out;
    uint32_t rows;
    if (!c.Get(&rows)) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT too short");
    }
    if (rows != x.chordNotes) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT has %u records, the SNG %u", rows, x.chordNotes);
    }

    // Every cell has nine fixed bytes, even with no bends. Bound both the wire
    // count and the in-memory Tech array before any product or allocation.
    const size_t columns = out->strings.size();
    if (!columns || rows > (c.n - c.at) / (columns * 9) || rows > SIZE_MAX / sizeof(Tech) / columns) {
        return Fail(Status::Invalid, x.why, x.whyLen, "CHNT size");
    }

    const size_t cells = static_cast<size_t>(rows) * columns;

    out->tech.resize(cells);

    for (size_t i = 0; i < cells; ++i) {
        const Status s = ParseTech(x, c, i / out->strings.size(), out->tech[i]);
        if (s != Status::Ok) {
            return s;
        }
    }

    return Status::Ok;
}

// Section `s` at *at: checks its header and order, parses a known v1 section and skips an unknown one.
Status ParseSection(const Context& x, const uint8_t* p, uint32_t bsize, uint32_t s, size_t* at, Seen* seen) {
    if (bsize - *at < kSectionHeader) {
        return Fail(Status::Invalid, x.why, x.whyLen, "section %u: header past the end", s);
    }

    const uint8_t* id = p + *at;
    uint16_t ver, flags;
    uint32_t size;

    std::memcpy(&ver, p + *at + 4, 2);
    std::memcpy(&flags, p + *at + 6, 2);
    std::memcpy(&size, p + *at + 8, 4);
    *at += kSectionHeader;
    if (size > bsize - *at) {
        return Fail(Status::Invalid, x.why, x.whyLen, "section %u: %u bytes past the end", s, size);
    }

    Cursor c{p + *at, size};

    *at += size;
    const bool isStrs = Id(id, "STRS"), isChdt = Id(id, "CHDT"), isChnt = Id(id, "CHNT");
    if (s == 0 && !isStrs) {
        return Fail(Status::Invalid, x.why, x.whyLen, "the first section is not STRS");
    }

    uint32_t sectionId;

    std::memcpy(&sectionId, id, sizeof sectionId);
    if (!seen->Add(sectionId, p, size_t(id - p))) {
        return Fail(Status::Invalid, x.why, x.whyLen, "section %.4s twice", reinterpret_cast<const char*>(id));
    }

    if ((!isStrs && !isChdt && !isChnt) || ver != 1) {
        if (flags & kFlagCritical) {
            return Fail(Status::Unsupported, x.why, x.whyLen, "critical section %.4s v%u",
                        reinterpret_cast<const char*>(id), ver);
        }

        ++x.out->skipped;
        return Status::Ok;
    }

    Status r;

    if (isStrs) {
        r = ParseStrs(x, c);
    } else if (isChdt) {
        x.out->hasTemplates = true;
        r = ParseChdt(x, c);
    } else {
        x.out->hasChordNotes = true;
        r = ParseChnt(x, c);
    }

    if (r != Status::Ok) {
        return r;
    }
    if (c.at != c.n) {
        return Fail(Status::Invalid, x.why, x.whyLen, "section %.4s: %zu unread bytes",
                    reinterpret_cast<const char*>(id), c.n - c.at);
    }

    return Status::Ok;
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

    Header h;
    const Status header = ReadHeader(p, n, &h, why, whyLen);
    if (header != Status::Ok) {
        return header;
    }

    *out = Block();
    const Context x{templates, chordNotes, tuning, out, why, whyLen};
    size_t at = h.size;
    Seen seen{h.size};

    for (uint32_t s = 0; s < h.count; ++s) {
        const Status r = ParseSection(x, p, h.blockSize, s, &at, &seen);
        if (r != Status::Ok) {
            return r;
        }
    }

    if (at != h.blockSize) {
        return Fail(Status::Invalid, why, whyLen, "%zu bytes after the last section", h.blockSize - at);
    }
    if (out->stringCount == 0) {  // only a parsed, supported STRS supplies the required layout
        return Fail(Status::Invalid, why, whyLen, "no STRS section");
    }

    return Status::Ok;
}

}  // namespace rs7ext
