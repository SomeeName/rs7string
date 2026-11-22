// Malformed-input coverage of the real extension block parser (model/extblock.cpp) and its XB1 loader entry
// (features/nslot/hooks_extblock.cpp): every truncation, every boundary value at every offset, off-by-one
// at the end and a fixed-seed random mutation run. Inputs end at a PAGE_NOACCESS page, so any read past
// the block faults; accepted blocks must satisfy the spec's invariants.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
static bool oversized = false;
void* operator new(size_t n) {
    if (n > (1u << 20)) { oversized = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#include "model/extblock.cpp"
#include "platform/safecopy.cpp"
#include "features/nslot/hooks_extblock.cpp"
extern "C" void __cdecl RsxrMid_Fldcw(uint32_t) {}
void RsxrLog(const char*, ...) {}
void RsxrNs_OnSngLoaded(void*) {}
void RsxrNs_OnSongFree(void*) {}

using Bytes = std::vector<uint8_t>;
static void U8(Bytes& b, int v) { b.push_back(uint8_t(v)); }
static void U16(Bytes& b, unsigned v) { U8(b, v & 0xff); U8(b, v >> 8); }
static void U32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) U8(b, v >> (8 * i)); }
static void Id(Bytes& b, const char* id) { b.insert(b.end(), id, id + 4); }
static void Section(Bytes& b, const char* id, unsigned flags, const Bytes& payload) {
    Id(b, id); U16(b, 1); U16(b, flags); U32(b, uint32_t(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
}
static void Finish(Bytes& b, uint32_t count) {
    uint32_t size = uint32_t(b.size());
    std::memcpy(b.data() + 8, &size, 4); std::memcpy(b.data() + 12, &count, 4);
}
static Bytes Header() { Bytes b; Id(b, "RSXR"); U16(b, 1); U16(b, 16); U32(b, 0); U32(b, 0); return b; }

struct Sample { Bytes bytes; uint32_t templates, chordNotes, tuning; };

// Guitar with `cols` extended strings, `t` templates, `c` ChordNotes records (with bends) and an unknown section.
static Sample Guitar(int cols, uint32_t t, uint32_t c) {
    Bytes b = Header(), p = {0, uint8_t(6 + cols), uint8_t(cols), 0};
    for (int i = 0; i < cols; ++i) U8(p, 6 + i);
    Section(b, "STRS", 1, p);
    p.clear(); U32(p, t);
    for (uint32_t r = 0; r < t; ++r) for (int i = 0; i < cols; ++i) {
        int fret = (r + i) % 4 == 3 ? -1 : int((r * 5 + i) % 25);
        U8(p, fret); U8(p, fret < 0 ? -1 : int((r + i) % 6) - 1);
    }
    Section(b, "CHDT", 1, p);
    p.clear(); U32(p, c);
    for (uint32_t r = 0; r < c; ++r) for (int i = 0; i < cols; ++i) {
        unsigned bends = (r + i) % 3;
        U32(p, 0x2000u << i); U8(p, r % 2 ? 5 : -1); U8(p, -1); U16(p, i); U8(p, bends);
        for (unsigned k = 0; k < bends; ++k) { U32(p, 0x3f800000u); U32(p, 0x3f000000u); U32(p, 0); }
    }
    Section(b, "CHNT", 0, p);
    Section(b, "XTRA", 0, {1, 2, 3});
    Finish(b, 4);
    return {b, t, c, uint32_t(6 + cols)};
}
static Sample Bass() {
    Bytes b = Header();
    Section(b, "STRS", 1, {1, 5, 0, 0});
    Finish(b, 1);
    return {b, 3, 2, 6};
}

// Input ends exactly at a PAGE_NOACCESS page.
static uint8_t* g_end = nullptr;
static const size_t kRoom = 64 * 1024;
static bool Arena() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const size_t page = si.dwPageSize, room = (kRoom + page - 1) / page * page;
    uint8_t* m = static_cast<uint8_t*>(VirtualAlloc(nullptr, room + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old;
    if (!m || !VirtualProtect(m + room, page, PAGE_NOACCESS, &old)) return false;
    g_end = m + room;
    return true;
}

enum Outcome { kOk, kRejected, kFault, kThrow };
static int ParseCatch(const uint8_t* p, size_t n, const Sample& s, uint32_t t, uint32_t c, uint32_t u,
                      rsxrext::Block* out, char* why) {
    try {
        return rsxrext::Parse(p, n, t, c, u, out, why, 160) == rsxrext::Status::Ok ? kOk : kRejected;
    } catch (...) { return kThrow; }
}
static int Guarded(const uint8_t* p, size_t n, const Sample& s, uint32_t t, uint32_t c, uint32_t u,
                   rsxrext::Block* out, char* why) {
    __try { return ParseCatch(p, n, s, t, c, u, out, why); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
}

static const char* Invariants(const rsxrext::Block& b, uint32_t t, uint32_t c, uint32_t u) {
    const size_t cols = b.strings.size();
    if (b.instrument == rsxrext::kInstrumentBass) {
        if (b.stringCount < 5 || b.stringCount > 6 || cols || u != 6) return "bass layout";
        if (b.hasTemplates || b.hasChordNotes || !b.fret.empty() || !b.tech.empty()) return "bass with columns data";
    } else {
        if (b.instrument != 0 || b.stringCount < 7 || b.stringCount > rsxrext::kMaxStrings) return "string count";
        if (uint32_t(b.stringCount) != u || cols != size_t(b.stringCount - 6)) return "columns vs tuning";
        for (size_t i = 0; i < cols; ++i) if (b.strings[i] != int(6 + i)) return "column string";
    }
    if (b.fret.size() != b.finger.size()) return "fret/finger sizes";
    if (b.hasTemplates ? b.fret.size() != size_t(t) * cols : !b.fret.empty()) return "CHDT cell count";
    for (size_t i = 0; i < b.fret.size(); ++i) {
        int f = b.fret[i], g = b.finger[i];
        if (f < -1 || f > rsxrext::kMaxFret || g < -1 || g > 4 || (f == -1 && g != -1)) return "CHDT cell value";
    }
    if (b.hasChordNotes ? b.tech.size() != size_t(c) * cols : !b.tech.empty()) return "CHNT cell count";
    for (const auto& x : b.tech) {
        if (x.bends.size() > rsxrext::kMaxBends) return "bend count";
        if (x.slideTo < -1 || x.slideTo > rsxrext::kMaxFret || x.slideUnpitchTo < -1 ||
            x.slideUnpitchTo > rsxrext::kMaxFret) return "slide value";
    }
    for (uint32_t r : {0u, 1u, t, t + 1, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
        for (int s = -1; s <= rsxrext::kMaxStrings + 1; ++s) {
            int f = 99, g = 99;
            if (b.TemplateColumn(r, s, &f, &g) && (r >= t || f < 0 || f > rsxrext::kMaxFret || g < -1 || g > 4))
                return "TemplateColumn out of range";
        }
    }
    return nullptr;
}

static unsigned g_ok = 0, g_rejected = 0;
// Parses `v` (placed against the guard page) and checks the outcome; `mustReject` for known-bad inputs.
static bool Try(const char* what, const Bytes& v, const Sample& s, uint32_t t, uint32_t c, uint32_t u,
                bool mustReject) {
    if (v.size() > kRoom) return true;
    uint8_t* p = g_end - v.size();
    if (!v.empty()) std::memcpy(p, v.data(), v.size());
    rsxrext::Block b; char why[160] = "unset";
    oversized = false;
    const int r = Guarded(p, v.size(), s, t, c, u, &b, why);
    const char* bad = nullptr;
    if (r == kFault) bad = "access violation (read past the input)";
    else if (r == kThrow) bad = "exception escaped Parse";
    else if (oversized) bad = "oversized allocation before validation";
    else if (r == kOk && mustReject) bad = "accepted";
    else if (r == kOk) bad = Invariants(b, t, c, u);
    else if (!v.empty() && !why[0]) bad = "rejected without a reason";
    if (bad) {
        std::printf("FAIL %s: %s (n=%zu, counts %u/%u/%u, why: %s)\n  bytes:", what, bad, v.size(), t, c, u, why);
        for (size_t i = 0; i < v.size() && i < 256; ++i) std::printf(" %02x", v[i]);
        std::puts("");
        return false;
    }
    ++(r == kOk ? g_ok : g_rejected);
    return true;
}

static uint32_t g_rng = 0x7a11c0deu;
static uint32_t Rand() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static const uint32_t kEdges[] = {0, 1, 2, 6, 7, 8, 9, 12, 16, 24, 25, 32, 33, 0x7f, 0x80, 0xfe, 0xff, 0x100,
                                  0xffff, 0x10000, 0x7fffffff, 0x80000000u, 0xfffffff0u, 0xfffffffeu, 0xffffffffu};

int main() {
    if (!Arena()) return 90;
    const Sample samples[] = {Guitar(1, 2, 1), Guitar(2, 3, 2), Guitar(10, 1, 1), Guitar(1, 0, 0), Bass()};
    for (const Sample& s : samples) {
        const unsigned acceptedBefore = g_ok;
        if (!Try("valid sample", s.bytes, s, s.templates, s.chordNotes, s.tuning, false)) return 1;
        if (g_ok != acceptedBefore + 1) { std::puts("FAIL: valid sample rejected"); return 1; }
    }
    unsigned okBefore = g_ok;
    for (const Sample& s : samples) {
        const Bytes& v = s.bytes;
        const uint32_t t = s.templates, c = s.chordNotes, u = s.tuning;
        // Song counts that disagree with the block.
        for (uint32_t d : {1u, 0xffffffffu}) {  // bass blocks carry no template / ChordNotes data
            if (!Try("tuning mismatch", v, s, t, c, u + d, true)) return 2;
            if (&s != &samples[4] && !Try("templates mismatch", v, s, t + d, c, u, true)) return 2;
            if (&s != &samples[4] && !Try("chordNotes mismatch", v, s, t, c + d, u, true)) return 2;
        }
        for (size_t n = 0; n < v.size(); ++n) {
            Bytes cut(v.begin(), v.begin() + n);
            if (!Try("truncation", cut, s, t, c, u, true)) return 3;
            if (n >= 12) {  // the header's block size made to agree with the truncation
                uint32_t size = uint32_t(n); std::memcpy(cut.data() + 8, &size, 4);
                if (!Try("consistent truncation", cut, s, t, c, u, true)) return 4;
            }
        }
        for (int extra : {1, 2, 9}) {  // off by one (and more) at the end, with and without the size fixed up
            Bytes longer = v; longer.insert(longer.end(), size_t(extra), uint8_t(0));
            if (!Try("trailing byte", longer, s, t, c, u, true)) return 5;
            uint32_t size = uint32_t(longer.size()); std::memcpy(longer.data() + 8, &size, 4);
            if (!Try("trailing byte, size fixed", longer, s, t, c, u, true)) return 6;
        }
        for (size_t at = 0; at < v.size(); ++at) for (uint32_t e : kEdges) for (size_t w : {1u, 2u, 4u}) {
            if (at + w > v.size()) continue;
            Bytes m = v; std::memcpy(m.data() + at, &e, w);
            if (!Try("boundary value", m, s, t, c, u, false)) return 7;
        }
    }
    unsigned random = 0;
    for (; random < 40000; ++random) {
        const Sample& s = samples[Rand() % (sizeof samples / sizeof samples[0])];
        Bytes v = s.bytes;
        for (unsigned ops = 1 + Rand() % 4; ops--;) {
            const size_t n = v.size(), at = n ? Rand() % n : 0;
            switch (Rand() % 7) {
                case 0: if (n) v[at] ^= uint8_t(1u << (Rand() % 8)); break;
                case 1: if (n) v[at] = uint8_t(Rand()); break;
                case 2: if (at + 4 <= n) { uint32_t e = kEdges[Rand() % 25]; std::memcpy(v.data() + at, &e, 4); } break;
                case 3: v.resize(n ? Rand() % n : 0); break;
                case 4: for (unsigned k = 1 + Rand() % 16; k--;) v.insert(v.begin() + at, uint8_t(Rand())); break;
                case 5: if (n) v.erase(v.begin() + at, v.begin() + at + 1 + Rand() % (n - at)); break;
                case 6: if (n) { size_t len = 1 + Rand() % (n - at); Bytes d(v.begin() + at, v.begin() + at + len);
                                 v.insert(v.begin() + Rand() % (n + 1), d.begin(), d.end()); } break;
            }
        }
        if (v.size() >= 12 && Rand() % 2) { uint32_t size = uint32_t(v.size()); std::memcpy(v.data() + 8, &size, 4); }
        uint32_t t = s.templates, c = s.chordNotes, u = s.tuning;
        if (Rand() % 8 == 0) t = kEdges[Rand() % 25];
        if (Rand() % 8 == 0) c = kEdges[Rand() % 25];
        if (Rand() % 8 == 0) u = kEdges[Rand() % 25];
        if (!Try("random mutation", v, s, t, c, u, false)) { std::printf("  iteration %u\n", random); return 8; }
    }

    // XB1 entry: the stream's buffer/position/size come from the game; the vectors from the loaded song.
    const Sample& s = samples[1];
    static uint8_t song[0x120], stream[0x1c], templates[3 * rsxrns::kTplSize], tuning[16], cn[2 * rsxrns::kCnSize];
    auto Vec2 = [](size_t off, const void* p, size_t n) {
        uintptr_t v[2] = {reinterpret_cast<uintptr_t>(p), reinterpret_cast<uintptr_t>(p) + n};
        std::memcpy(song + off, v, 8);
    };
    Vec2(0x94, templates, sizeof templates); Vec2(0x110, tuning, sizeof tuning); Vec2(0xac, cn, sizeof cn);
    const size_t prefix = 40;  // stock sections before the block
    uint8_t* body = g_end - prefix - s.bytes.size();
    std::memcpy(body + prefix, s.bytes.data(), s.bytes.size());
    struct Case { const char* name; uintptr_t buf; uint32_t pos, size; bool stored; } cases[] = {
        {"valid", uintptr_t(body), prefix, uint32_t(prefix + s.bytes.size()), true},
        {"no trailing bytes", uintptr_t(body), prefix, prefix, false},
        {"position past size", uintptr_t(body), prefix + 1, prefix, false},
        {"position one short", uintptr_t(body), prefix - 1, uint32_t(prefix + s.bytes.size()), false},
        {"size one past the buffer", uintptr_t(body), prefix, uint32_t(prefix + s.bytes.size() + 1), false},
        {"size past the trailing limit", uintptr_t(body), 0, 0xffffffffu, false},
        {"trailing limit + 1", uintptr_t(body), 0, (16u << 20) + 1, false},
        {"null buffer", 0, prefix, uint32_t(prefix + s.bytes.size()), false},
        {"wrapping buffer", ~uintptr_t(0) - 7, prefix, uint32_t(prefix + s.bytes.size()), false},
    };
    for (const Case& k : cases) {
        std::memcpy(stream + 0x10, &k.buf, 4); std::memcpy(stream + 0x14, &k.pos, 4); std::memcpy(stream + 0x18, &k.size, 4);
        RsxrExt_Store(song, std::make_shared<rsxrext::Block>());  // a stale block must never survive a failed load
        oversized = false;
        RsxrExt_OnSngLoaded(song, stream);
        if (bool(RsxrExt_Find(song)) != k.stored || oversized) {
            std::printf("FAIL: XB1 case '%s' stored=%d oversized=%d\n", k.name, int(bool(RsxrExt_Find(song))), int(oversized));
            return 9;
        }
    }
    {  // end < begin
        uintptr_t v[2] = {uintptr_t(templates) + 8, uintptr_t(templates)}; std::memcpy(song + 0x94, v, 8); }
    std::memcpy(stream + 0x10, &cases[0].buf, 4); std::memcpy(stream + 0x14, &cases[0].pos, 4); std::memcpy(stream + 0x18, &cases[0].size, 4);
    RsxrExt_OnSngLoaded(song, stream);
    if (RsxrExt_Find(song)) { std::puts("FAIL: inverted template vector accepted"); return 10; }

    std::printf("PASS: %u accepted / %u rejected (%u random mutations); no faults past the input, no oversized "
                "allocation; XB1 stream table fail-closed\n", g_ok - okBefore, g_rejected, random);
}
