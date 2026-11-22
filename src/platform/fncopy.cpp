#include "platform/fncopy.h"
#include "platform/midhook.h"
#include "platform/pattern.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {
// One executable arena for every copy (about 7 KB of code today).
uint8_t* g_arena = nullptr;
uint32_t g_used = 0;
constexpr uint32_t kArena = 0x10000;

uint8_t* Take(uint32_t n) {
    if (!g_arena) {
        g_arena =
            static_cast<uint8_t*>(VirtualAlloc(nullptr, kArena, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_arena) {
            return nullptr;
        }
    }

    n = (n + 15u) & ~15u;
    if (g_used + n > kArena) {
        return nullptr;
    }

    uint8_t* p = g_arena + g_used;

    g_used += n;

    return p;
}

// Exactly `len` fixed hex bytes ("8B 74 BB 04", no wildcards) into out.
bool HexBytes(const char* s, int len, uint8_t* out) {
    RsxrPattern p;
    if (!Rsxr_ParsePattern(s, p) || p.bytes.size() != static_cast<size_t>(len)) {
        return false;
    }

    for (const bool w : p.wild) {
        if (w) {
            return false;
        }
    }

    std::memcpy(out, p.bytes.data(), p.bytes.size());

    return true;
}

void Rel32(uint8_t* at, uintptr_t insnEnd, uintptr_t target) {
    const int32_t rel = int32_t(target - insnEnd);

    std::memcpy(at, &rel, 4);
}
}  // namespace

uint32_t RsxrFn_Fnv(const uint8_t* p, uint32_t n) {
    uint32_t h = 0x811C9DC5u;

    for (uint32_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 0x01000193u;
    }

    return h;
}

bool RsxrFn_Build(const RsxrFnSpec& spec, uintptr_t live, uintptr_t* copy, uintptr_t* tramp, char* why, int whyLen) {
    auto fail = [&](const char* m) {
        std::snprintf(why, size_t(whyLen), "%s %s", spec.id, m);

        return false;
    };
    if (live != spec.va) {
        return fail("entry site is not at the generated address");
    }

    const uint8_t* src = reinterpret_cast<const uint8_t*>(live);
    if (RsxrFn_Fnv(src, spec.len) != spec.fnv) {
        return fail("live bytes differ from the unpacked exe");
    }

    uint8_t buf[0x800];
    if (spec.len > sizeof buf || spec.entry < 5 || spec.entry > 16) {
        return fail("bad spec");
    }

    std::memcpy(buf, src, spec.len);

    for (int i = 0; i < spec.nEdits; ++i) {
        const RsxrFnEdit& e = spec.edits[i];
        uint8_t exp[16], wr[16];
        if (uint32_t(e.off) + e.len > spec.len || e.len > 16 || !HexBytes(e.expect, e.len, exp) ||
            std::memcmp(buf + e.off, exp, e.len) != 0) {
            return fail("edit bytes do not match");
        }

        if (e.sym || e.callin) {
            if (e.len < 5) {
                return fail("call edit shorter than 5");
            }

            if (e.callin && !e.callin->Entry()) {
                return fail("call-in entry not built");
            }

            std::memset(buf + e.off, 0x90, e.len);
            buf[e.off] = 0xE8;
        } else {
            if (!HexBytes(e.write, e.len, wr)) {
                return fail("bad edit");
            }

            std::memcpy(buf + e.off, wr, e.len);
        }
    }

    uint8_t* c = Take(spec.len);
    uint8_t* t = Take(spec.entry + 5);
    if (!c || !t) {
        return fail("no executable memory");
    }

    std::memcpy(c, buf, spec.len);
    const uintptr_t cb = reinterpret_cast<uintptr_t>(c);

    for (int i = 0; i < spec.nEdits; ++i) {
        const RsxrFnEdit& e = spec.edits[i];

        if (e.sym || e.callin) {
            Rel32(c + e.off + 1, cb + e.off + 5, e.callin ? e.callin->Entry() : reinterpret_cast<uintptr_t>(e.sym));
        }
    }

    for (int i = 0; i < spec.nRels; ++i) {
        const RsxrFnRel& r = spec.rels[i];

        Rel32(c + r.off, cb + r.off + 4, r.target - spec.va + live);
    }

    std::memcpy(t, src, spec.entry);
    t[spec.entry] = 0xE9;
    Rel32(t + spec.entry + 1, reinterpret_cast<uintptr_t>(t) + spec.entry + 5, live + spec.entry);
    FlushInstructionCache(GetCurrentProcess(), c, spec.len);
    FlushInstructionCache(GetCurrentProcess(), t, spec.entry + 5);
    *copy = cb;
    *tramp = reinterpret_cast<uintptr_t>(t);

    return true;
}
