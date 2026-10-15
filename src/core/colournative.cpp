#include "core/colournative.h"
#include "core/color.h"
#include "core/string7.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_colours = 0;

namespace {
// Only extra RGB values are owned here. Stock source-list pointers are never
// copied/replaced/freed. Static storage survives teardown, rebuild, mode switches.
float g_rgb[2][28][2][3] = {};
uint32_t g_fallback7 = 0xC8C8C8;
constexpr uint32_t kPlaceholder[2] = {0x00D6B2, 0xFF8C00};  // string 8: teal / CB orange

template <class T>
T& At(uint8_t* p, uint32_t o) {
    return *reinterpret_cast<T*>(p + o);
}

float Max(const float* c) {
    return c ? std::max(c[0], std::max(c[1], c[2])) : 0.0f;
}

bool PerString(int state) {
    return state <= 4 || state == 7 || state == 8 || state == 11 || state == 12 || state == 13 || state == 15 ||
           state == 16 || state == 17 || state >= 25;
}

uint32_t NativeUV(uint32_t mode, uint32_t state, uint32_t slot) {
    return rs7colour::kUV + 4 * ((mode * 28 + state) * 8 + slot);
}

void PutBits(uint8_t* mgr, uint32_t off, uint32_t bits) {
    At<uint32_t>(mgr, off) = bits;
}
}  // namespace

const float* rs7colour::GetRGB(const uint8_t* mgr, uint32_t mode, uint32_t state, uint32_t slot) {
    if (!mgr || mode >= 2 || state >= 28 || slot >= Width()) {
        return nullptr;
    }

    return *reinterpret_cast<const float* const*>(mgr + PtrOffset(mode, state, slot));
}

void* rs7colour::ExistingManager(uintptr_t ref) {
    return ref ? *reinterpret_cast<void* const volatile*>(ref) : nullptr;
}

void rs7colour::Publish(uint8_t* mgr) {
    if (!g_rs7_native_colours || !mgr) {
        return;
    }

    g_fallback7 = (uint32_t(g_rs7_fx_rgb[0] * 255.0f) << 16) | (uint32_t(g_rs7_fx_rgb[1] * 255.0f) << 8) |
                  uint32_t(g_rs7_fx_rgb[2] * 255.0f);

    for (uint32_t mode = 0; mode < 2; ++mode) {
        float dim = rs7s7::kDefaultDim, total = 0.0f;
        int count = 0;

        for (int s = 0; s < 6; ++s) {
            const float* c = GetRGB(mgr, mode, 2, s);

            if (c) {
                total += Max(c);
                ++count;
            }
        }

        if (count && total > 0.0f) {
            dim = total / float(count);
        }

        for (uint32_t state = 0; state < 28; ++state) {
            const float* stock = GetRGB(mgr, mode, state, 0);
            const float baseline = Max(GetRGB(mgr, mode, 1, 0));

            for (int extra = 0; extra < 2; ++extra) {
                float* out = g_rgb[mode][state][extra];

                if (stock) {
                    std::memcpy(out, stock, 12);
                } else {
                    std::memset(out, 0, 12);
                }

                if (PerString(state)) {
                    float lit[3];

                    if (extra == 0) {
                        std::memcpy(lit, g_rs7_fx_rgb + mode * 3, 12);
                    } else {
                        Rs7_RgbToFloats(kPlaceholder[mode], lit);
                    }
                    std::memcpy(out, lit, 12);

                    if (state == 2) {
                        Rs7S7_DimColour(lit, dim, out);
                    } else if (state == 3) {
                        Rs7S7_GlowColour(lit, out);
                    } else if ((state == 0 || state == 4 || state == 7 || state == 8) && baseline > 0.0f) {
                        const float scale = Max(stock) / baseline;

                        for (int c = 0; c < 3; ++c) {
                            out[c] = lit[c] * scale;
                        }
                    }
                }

                // Capacity defaults also exist for shared/unknown states; their
                // existing indices and authored arities are not widened.
                At<float*>(mgr, PtrOffset(mode, state, 6 + extra)) = out;
            }
        }

        // Authored texture rows, never extrapolate UV or change denominators:
        // string 7 uses the recoloured ramp row; string 8 deliberately borrows
        // stock string 5's row until its own atlas band is authored.
        for (uint32_t state : {15u, 17u, 21u}) {
            const float v = g_rs7_v6[mode] != 0.0f ? g_rs7_v6[mode] : At<float>(mgr, NativeUV(mode, state, 0));

            At<float>(mgr, NativeUV(mode, state, 6)) = v;
            At<float>(mgr, NativeUV(mode, state, 7)) = At<float>(mgr, NativeUV(mode, state, 5));
        }

        // Wire UV denominator 6; scale-marker state14 is an index, not RGB.
        for (uint32_t state : {11u, 14u}) {
            PutBits(mgr, NativeUV(mode, state, 6), At<uint32_t>(mgr, NativeUV(mode, state, 0)));
            PutBits(mgr, NativeUV(mode, state, 7), At<uint32_t>(mgr, NativeUV(mode, state, 5)));
        }
    }
}

// Publish() reads the stock colour pointers out of the manager. The game thread builds the manager (CMINIT,
// CMBUILD); publishing from any other thread can meet a half-built manager -> access violation on that thread
// (R2b, evidence/L1_7test.log "DIAG ... AV", the ramp thread died and the game hung; review #5).
// Rules (review #5, docs/ARCHITECTURE.md):
//  - only the thread that ran CMBUILD (g_cmTid, the game/render thread) ever writes into the manager;
//  - every other caller only sets g_dirty (RequestPublish). The ramp worker never calls PublishCurrent:
//    StoreV6 (rampstate.cpp) requests, and the game thread drains in CMBUILD, FGHK (0x592AC0, every guitar
//    update) and the SetTexture hook (render thread) via DrainPending;
//  - PublishCurrent (settings overlay, Present thread) still publishes directly when it runs on that thread,
//    and logs once + defers when it does not (the "assert").
// An SEH guard stays as the last net.
static void* volatile g_built = nullptr;
static volatile LONG g_dirty = 0;
static volatile DWORD g_cmTid = 0;  // thread that ran the last CMBUILD (0: none yet)
static LONG g_loggedFault = 0, g_loggedForeign = 0;
static void* volatile g_loggedInit = nullptr;
static void* volatile g_loggedBuild = nullptr;

static bool PublishGuarded(uint8_t* mgr) {
    __try {
        rs7colour::Publish(mgr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!InterlockedExchange(&g_loggedFault, 1)) {
            Rs7Log("colour: publish faulted on thread %lu (manager %p) - skipped", GetCurrentThreadId(),
                   static_cast<void*>(mgr));
        }
        return false;
    }
}

void rs7colour::RequestPublish() {
    InterlockedExchange(&g_dirty, 1);
}

bool rs7colour::PublishPending() {
    return g_dirty != 0;
}

void rs7colour::DrainPending() {
    if (!g_dirty || !g_rs7_native_colours || !g_rs7_colormgr) {
        return;
    }
    if (g_cmTid == 0 || GetCurrentThreadId() != g_cmTid) {
        return;  // not the manager's thread: leave it pending
    }

    uint8_t* mgr = *reinterpret_cast<uint8_t**>(g_rs7_colormgr);
    if (!mgr || g_built != mgr) {
        return;  // CMBUILD drains when it finishes this one
    }

    if (InterlockedExchange(&g_dirty, 0)) {
        PublishGuarded(mgr);
    }
}

void rs7colour::PublishCurrent() {
    if (!g_rs7_native_colours || !g_rs7_colormgr) {
        return;
    }

    const DWORD tid = GetCurrentThreadId();

    if (g_cmTid != 0 && tid != g_cmTid) {  // not the game thread: never touch the manager here
        if (!InterlockedExchange(&g_loggedForeign, 1)) {
            Rs7Log("colour: PublishCurrent on thread %lu, the manager belongs to thread %lu - deferred to it (logged "
                   "once)",
                   tid, static_cast<unsigned long>(g_cmTid));
        }

        RequestPublish();
        return;
    }

    uint8_t* mgr = *reinterpret_cast<uint8_t**>(g_rs7_colormgr);
    if (!mgr) {
        return;
    }

    if (g_built != mgr) {  // not (yet) fully built by the game thread: CMBUILD will pick the new values up
        RequestPublish();
        return;
    }

    InterlockedExchange(&g_dirty, 0);
    PublishGuarded(mgr);
}

void __cdecl Rs7Colour_CMINIT(Rs7Mid::Context& c) {
    g_built = nullptr;
    void* const mgr = reinterpret_cast<void*>(c.esi);

    if (g_loggedInit != mgr) {  // L6, once per manager
        g_loggedInit = mgr;
        Rs7Log("colour: CMINIT %p tid %lu", mgr, GetCurrentThreadId());
    }

    rs7colour::Publish(reinterpret_cast<uint8_t*>(c.esi));
}

void __cdecl Rs7Colour_CMBUILD(Rs7Mid::Context& c) {
    auto* mgr = reinterpret_cast<uint8_t*>(c.esi);

    g_cmTid = GetCurrentThreadId();

    if (g_loggedBuild != mgr) {  // L6, once per manager
        g_loggedBuild = mgr;
        Rs7Log("colour: CMBUILD %p tid %lu", static_cast<void*>(mgr), GetCurrentThreadId());
    }

    InterlockedExchange(&g_dirty, 0);  // this publish carries every request made so far
    rs7colour::Publish(mgr);
    g_built = mgr;
    if (InterlockedExchange(&g_dirty, 0)) {
        rs7colour::Publish(mgr);  // a request arrived meanwhile
    }
}

void rs7colour::ResetThreadForTests() {
    g_cmTid = 0;
    g_dirty = 0;
    g_built = nullptr;
    g_loggedForeign = 0;
}

void __cdecl Rs7Colour_CMUV(Rs7Mid::Context& c) {
    // 518BC0: ST0 equals the bits in [EBP+14h], EAX=(mode*28+state)*8.
    // Replace first FST via integer store and skip it; preserve the x87 stack.
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0x14);

    for (uint32_t slot : {0u, 6u, 7u}) {
        PutBits(mgr, rs7colour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518BC7;
}

void __cdecl Rs7Colour_CMIX(Rs7Mid::Context& c) {
    // 518CA4: EDX=mode*28+14. Replace LEA *3 / ADD *2 with index*8.
    c.eax = c.edx * 8;
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0xC);
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);

    for (uint32_t slot : {6u, 7u}) {
        PutBits(mgr, rs7colour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518CA9;
}

void __cdecl Rs7Colour_CMFALL(Rs7Mid::Context& c) {
    // 75835A: bounded fallback before the guard; no six-slot stack OOB.
    if (c.edx < 6) {
        c.eax = *reinterpret_cast<const uint32_t*>(c.ebp + c.edx * 4 - 0x1C);
    } else if (c.edx == 6) {
        c.eax = g_fallback7;
    } else {
        c.eax = c.edx == 7 ? kPlaceholder[0] : 0;
    }
    c.esp -= 4;
    *reinterpret_cast<uint32_t*>(c.esp) = c.esi;  // replay PUSH ESI
    c.jump = 0x75835F;
}
