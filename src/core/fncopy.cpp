#include "core/fncopy.h"
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

int Hex(const char* s, uint8_t* out, int max) {
    int n = 0;

    while (*s && n < max) {
        while (*s == ' ') {
            ++s;
        }

        if (!s[0] || !s[1]) {
            break;
        }

        unsigned v = 0;
        if (std::sscanf(s, "%2x", &v) != 1) {
            return -1;
        }

        out[n++] = uint8_t(v);
        s += 2;
    }

    return n;
}

void Rel32(uint8_t* at, uintptr_t insnEnd, uintptr_t target) {
    const int32_t rel = int32_t(target - insnEnd);

    std::memcpy(at, &rel, 4);
}
}  // namespace

uint32_t Rs7Fn_Fnv(const uint8_t* p, uint32_t n) {
    uint32_t h = 0x811C9DC5u;

    for (uint32_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 0x01000193u;
    }

    return h;
}

bool Rs7Fn_Build(const Rs7FnSpec& spec, uintptr_t live, uintptr_t* copy, uintptr_t* tramp, char* why, int whyLen) {
    auto fail = [&](const char* m) {
        std::snprintf(why, size_t(whyLen), "%s %s", spec.id, m);

        return false;
    };
    if (live != spec.va) {
        return fail("entry site is not at the generated address");
    }

    const uint8_t* src = reinterpret_cast<const uint8_t*>(live);
    if (Rs7Fn_Fnv(src, spec.len) != spec.fnv) {
        return fail("live bytes differ from the unpacked exe");
    }

    uint8_t buf[0x800];
    if (spec.len > sizeof buf || spec.entry < 5 || spec.entry > 16) {
        return fail("bad spec");
    }

    std::memcpy(buf, src, spec.len);

    for (int i = 0; i < spec.nEdits; ++i) {
        const Rs7FnEdit& e = spec.edits[i];
        uint8_t exp[16], wr[16];
        if (e.off + e.len > spec.len || e.len > 16 || Hex(e.expect, exp, 16) != e.len ||
            std::memcmp(buf + e.off, exp, e.len) != 0) {
            return fail("edit bytes do not match");
        }

        if (e.sym) {
            if (e.len < 5) {
                return fail("call edit shorter than 5");
            }

            std::memset(buf + e.off, 0x90, e.len);
            buf[e.off] = 0xE8;
        } else {
            if (Hex(e.write, wr, 16) != e.len) {
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
        const Rs7FnEdit& e = spec.edits[i];

        if (e.sym) {
            Rel32(c + e.off + 1, cb + e.off + 5, reinterpret_cast<uintptr_t>(e.sym));
        }
    }

    for (int i = 0; i < spec.nRels; ++i) {
        const Rs7FnRel& r = spec.rels[i];

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
