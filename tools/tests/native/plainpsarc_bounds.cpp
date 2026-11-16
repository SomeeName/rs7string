// Plain SNG entry at the edges of both buffers: source and output each end at a PAGE_NOACCESS page, so a copy
// one byte too long (or a header read past the source) faults. Table of lengths/capacities plus a fixed-seed run.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "features/psarc/psarc.cpp"
uintptr_t g_rs7_target_PP_SNGD = 0;
#include "sngstub.inc"
static int decrypts = 0;
bool __cdecl Decrypt(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t) { ++decrypts; return false; }

static uint8_t* GuardedEnd(size_t room) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const size_t page = si.dwPageSize; room = (room + page - 1) / page * page;
    uint8_t* m = static_cast<uint8_t*>(VirtualAlloc(nullptr, room + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old;
    return m && VirtualProtect(m + room, page, PAGE_NOACCESS, &old) ? m + room : nullptr;
}
enum { kFalse, kTrue, kFault };
static int Call(const uint8_t* src, uint32_t len, uint8_t* out, uint32_t outLen) {
    __try { return Rs7_Stub_PP_SNGD(src, len, nullptr, 0, out, outLen) ? kTrue : kFalse; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
}

static uint8_t *g_src, *g_out;
// An 8-byte header (magic, flags) + `len` bytes of body ending at the guard; `outLen` bytes of output ending at its guard.
static int Run(uint32_t magic, uint32_t flags, uint32_t len, uint32_t outLen) {
    uint8_t* body = g_src - len;
    std::memcpy(body - 8, &magic, 4); std::memcpy(body - 4, &flags, 4);
    for (uint32_t i = 0; i < len; ++i) body[i] = uint8_t(i * 7 + 1);
    uint8_t* out = g_out - outLen;
    std::memset(out, 0xcc, outLen);
    const int r = Call(body, len, out, outLen);
    if (r == kTrue && magic == 0x4a && flags == 1 && std::memcmp(out, body + 16, len - 16)) return -1;
    return r;
}

int main() {
    g_src = GuardedEnd(1 << 16); g_out = GuardedEnd(1 << 16);
    if (!g_src || !g_out) return 90;
    g_rs7_target_PP_SNGD = reinterpret_cast<uintptr_t>(&Decrypt);
    for (uint32_t len = 0; len <= 0x40; ++len) {
        for (int d = -2; d <= 2; ++d) {
            const int64_t want = int64_t(len) - 16 + d;
            if (want < 0) continue;
            const uint32_t outLen = uint32_t(want);
            const int r = Run(0x4a, 1, len, outLen);
            const int expected = len >= 16 && d >= 0 ? kTrue : kFalse;
            if (r != expected) {
                std::printf("FAIL: plain len %u outLen %u -> %d (want %d; -1 = wrong bytes, 2 = fault)\n", len, outLen, r, expected);
                return 1;
            }
        }
        decrypts = 0;  // not plain: length still gates before the header read; otherwise the game's decrypt runs
        for (uint32_t flags : {0u, 2u, 3u, 0xffffffffu}) if (Run(0x4a, flags, len, 0x40) == kFault) return 2;
        if (Run(0x4b, 1, len, 0x40) == kFault) return 2;
        if (decrypts != (len >= 16 ? 5 : 0)) { std::printf("FAIL: len %u routed %d decrypts\n", len, decrypts); return 3; }
    }
    // Pure decision table at the 32-bit edges.
    struct Row { uint32_t magic, flags, len, outLen; Rs7SngAction want; } rows[] = {
        {0x4a, 1, 0, 0xffffffffu, Rs7SngAction::Reject},      {0x4a, 1, 15, 0xffffffffu, Rs7SngAction::Reject},
        {0x4a, 1, 16, 0, Rs7SngAction::Copy},                 {0x4a, 1, 0xffffffffu, 0xffffffeeu, Rs7SngAction::Reject},
        {0x4a, 1, 0xffffffffu, 0xffffffefu, Rs7SngAction::Copy}, {0x4a, 1, 0x80000000u, 0x7fffffefu, Rs7SngAction::Reject},
        {0x4a, 0, 16, 0, Rs7SngAction::Decrypt},              {0, 1, 16, 0, Rs7SngAction::Decrypt},
        {0x4a00004a, 1, 16, 0, Rs7SngAction::Decrypt},        {0x4a, 0x101, 16, 0, Rs7SngAction::Decrypt},
    };
    for (const Row& r : rows) if (Rs7Plain_SngAction(r.magic, r.flags, r.len, r.outLen) != r.want) {
        std::printf("FAIL: action %x/%x/%u/%u\n", r.magic, r.flags, r.len, r.outLen); return 4;
    }
    uint32_t rng = 0x50a6c0deu;
    for (int i = 0; i < 20000; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const uint32_t len = rng % 4096, outLen = (rng >> 12) % 4096;
        const int r = Run(0x4a, 1, len, outLen);
        if (r != (len >= 16 && outLen >= len - 16 ? kTrue : kFalse)) {
            std::printf("FAIL: random len %u outLen %u -> %d\n", len, outLen, r); return 5;
        }
    }
    std::puts("PASS: plain copy exact at both buffer ends; short/oversized lengths rejected; routing intact");
}
