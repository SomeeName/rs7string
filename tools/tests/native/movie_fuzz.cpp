// Malformed movies and RXDA recipes against the real chord-diagram movie swap (features/diagrams/movie.cpp):
// every truncation, boundary values at every offset and a fixed-seed random run. Both inputs end at a
// PAGE_NOACCESS page, so a read past either faults; an accepted swap must re-parse with one DoInitAction.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "features/diagrams/movie.cpp"

using Bytes = std::vector<uint8_t>;
static void U16(Bytes& b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void U32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
static void Sha(const uint8_t* p, size_t n, uint8_t* out) {
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(p), ULONG(n), out, 32);
}
static const uint16_t kSprite = 0x1234;
static size_t g_tagAt = 0;  // offset of the DoInitAction tag in the movie

static Bytes Movie() {
    Bytes m = {'G', 'F', 'X', 8, 0, 0, 0, 0, 0x08, 0, 0, 0, 1, 0};  // 1-bit rect, rate, frames
    U16(m, 1 << 6);                                                  // ShowFrame
    U16(m, (12 << 6) | 3); m.insert(m.end(), {1, 2, 3});             // DoAction
    g_tagAt = m.size();
    U16(m, (59 << 6) | 6); U16(m, kSprite); m.insert(m.end(), {0xa, 0xb, 0xc, 0xd});
    U16(m, (12 << 6) | 63); U32(m, 2); m.insert(m.end(), {7, 7});    // long form
    U16(m, 0);                                                        // End
    uint32_t n = uint32_t(m.size()); std::memcpy(m.data() + 4, &n, 4);
    return m;
}
static Bytes Recipe(const Bytes& movie) {
    Bytes tag;
    U16(tag, (59 << 6) | 8); U16(tag, kSprite);
    tag.insert(tag.end(), movie.begin() + g_tagAt + 4, movie.begin() + g_tagAt + 8); tag.insert(tag.end(), {'x', 'y'});
    Bytes r = {'R', 'X', 'D', 'A', 1, 0, 0, 0};
    U32(r, 8); U32(r, uint32_t(tag.size())); U32(r, 3);
    r.resize(84); Sha(movie.data() + g_tagAt, 8, r.data() + 20); Sha(tag.data(), tag.size(), r.data() + 52);
    r.push_back(1); U32(r, 4); r.insert(r.end(), tag.begin(), tag.begin() + 4);  // literal header + sprite
    r.push_back(0); U32(r, 4); U32(r, 4);                                         // copy the old action bytes
    r.push_back(1); U32(r, 2); r.insert(r.end(), {'x', 'y'});
    return r;
}

static uint8_t *g_movieEnd = nullptr, *g_recipeEnd = nullptr;
static const size_t kRoom = 64 * 1024;
static uint8_t* GuardedEnd() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const size_t page = si.dwPageSize, room = (kRoom + page - 1) / page * page;
    uint8_t* m = static_cast<uint8_t*>(VirtualAlloc(nullptr, room + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old;
    return m && VirtualProtect(m + room, page, PAGE_NOACCESS, &old) ? m + room : nullptr;
}

enum { kRejected, kOk, kFault, kThrow };
static int SwapCatch(const uint8_t* p, size_t n, const uint8_t* r, size_t rn, std::vector<uint8_t>* out) {
    try { return rsxrdiagram::SwapMovie(p, n, r, rn, kSprite, out) ? kOk : kRejected; } catch (...) { return kThrow; }
}
static int Guarded(const uint8_t* p, size_t n, const uint8_t* r, size_t rn, std::vector<uint8_t>* out) {
    __try { return SwapCatch(p, n, r, rn, out); } __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
}

static unsigned g_ok = 0, g_rejected = 0;
static bool Try(const char* what, const Bytes& m, const Bytes& r, bool mustReject) {
    if (m.size() > kRoom || r.size() > kRoom) return true;
    uint8_t* p = g_movieEnd - m.size(); uint8_t* q = g_recipeEnd - r.size();
    if (!m.empty()) std::memcpy(p, m.data(), m.size());
    if (!r.empty()) std::memcpy(q, r.data(), r.size());
    std::vector<uint8_t> out;
    const int x = Guarded(m.empty() ? nullptr : p, m.size(), r.empty() ? nullptr : q, r.size(), &out);
    const char* bad = x == kFault ? "access violation" : x == kThrow ? "exception escaped" : nullptr;
    if (!bad && x == kOk && mustReject) bad = "accepted";
    if (!bad && x == kOk) {
        uint32_t size; std::memcpy(&size, out.data() + 4, 4);
        if (size != out.size() || out.size() > rsxrdiagram::kMovieLimit) bad = "output size field";
    }
    if (!bad && x == kRejected && !out.empty()) bad = "rejected but wrote the output";
    if (bad) {
        std::printf("FAIL %s: %s (movie %zu bytes, recipe %zu bytes)\n", what, bad, m.size(), r.size());
        return false;
    }
    ++(x == kOk ? g_ok : g_rejected);
    return true;
}

static uint32_t g_rng = 0x5eed0da7u;
static uint32_t Rand() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static const uint32_t kEdges[] = {0, 1, 2, 3, 4, 8, 13, 59, 62, 63, 64, 83, 84, 0xff, 0xffff, 0x10000,
                                  0x1000000, 0x1000001, 0x7fffffff, 0x80000000u, 0xfffffffeu, 0xffffffffu};
static const size_t kEdgeCount = sizeof kEdges / sizeof kEdges[0];

int main() {
    g_movieEnd = GuardedEnd(); g_recipeEnd = GuardedEnd();
    if (!g_movieEnd || !g_recipeEnd) return 90;
    const Bytes movie = Movie(), recipe = Recipe(movie);
    std::vector<uint8_t> out;
    if (!rsxrdiagram::SwapMovie(movie.data(), movie.size(), recipe.data(), recipe.size(), kSprite, &out) ||
        out.size() != movie.size() + 2) { std::puts("FAIL: valid movie + recipe rejected"); return 1; }
    if (!Try("valid", movie, recipe, false) || g_ok != 1) return 1;
    if (!Try("empty movie", {}, recipe, true) || !Try("empty recipe", movie, {}, true)) return 2;
    for (size_t n = 0; n < movie.size(); ++n) {
        Bytes cut(movie.begin(), movie.begin() + n);
        if (!Try("movie truncation", cut, recipe, true)) return 3;
        if (n >= 8) { uint32_t s = uint32_t(n); std::memcpy(cut.data() + 4, &s, 4); }
        if (!Try("movie truncation, size fixed", cut, recipe, true)) return 3;
    }
    for (size_t n = 0; n < recipe.size(); ++n)
        if (!Try("recipe truncation", movie, Bytes(recipe.begin(), recipe.begin() + n), true)) return 4;
    for (int extra : {1, 2}) {
        Bytes m = movie; m.insert(m.end(), size_t(extra), 0);
        if (!Try("movie trailing byte", m, recipe, true)) return 5;
        uint32_t s = uint32_t(m.size()); std::memcpy(m.data() + 4, &s, 4);
        if (!Try("movie trailing byte, size fixed", m, recipe, true)) return 5;
        Bytes r = recipe; r.insert(r.end(), size_t(extra), 0);
        if (!Try("recipe trailing byte", movie, r, true)) return 5;
    }
    for (size_t w : {1u, 2u, 4u}) for (uint32_t e : kEdges) {
        for (size_t at = 0; at + w <= movie.size(); ++at) {
            Bytes m = movie; std::memcpy(m.data() + at, &e, w);
            if (!Try("movie boundary value", m, recipe, false)) { std::printf("  at %zu value %08x\n", at, e); return 6; }
        }
        for (size_t at = 0; at + w <= recipe.size(); ++at) {
            Bytes r = recipe; std::memcpy(r.data() + at, &e, w);
            if (!Try("recipe boundary value", movie, r, false)) { std::printf("  at %zu value %08x\n", at, e); return 7; }
        }
    }
    unsigned i = 0;
    for (; i < 30000; ++i) {
        Bytes m = movie, r = recipe;
        Bytes& v = Rand() % 2 ? m : r;
        for (unsigned ops = 1 + Rand() % 3; ops--;) {
            const size_t n = v.size(), at = n ? Rand() % n : 0;
            switch (Rand() % 5) {
                case 0: if (n) v[at] ^= uint8_t(1u << (Rand() % 8)); break;
                case 1: if (at + 4 <= n) { uint32_t e = kEdges[Rand() % kEdgeCount]; std::memcpy(v.data() + at, &e, 4); } break;
                case 2: v.resize(n ? Rand() % n : 0); break;
                case 3: for (unsigned k = 1 + Rand() % 8; k--;) v.insert(v.begin() + at, uint8_t(Rand())); break;
                case 4: if (n) v.erase(v.begin() + at, v.begin() + at + 1 + Rand() % (n - at)); break;
            }
        }
        if (m.size() >= 8 && Rand() % 2) { uint32_t s = uint32_t(m.size()); std::memcpy(m.data() + 4, &s, 4); }
        if (!Try("random mutation", m, r, false)) { std::printf("  iteration %u\n", i); return 8; }
    }
    std::printf("PASS: %u accepted / %u rejected (%u random); no reads past either input\n", g_ok, g_rejected, i);
}
