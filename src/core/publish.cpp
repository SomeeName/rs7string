#include "core/publish.h"
#include "core/colournative.h"
#include "core/fncopy.h"
#include "core/fretgrid.h"
#include "core/gsnative.h"
#include "core/nextgrid.h"
#include "core/pitch8.h"
#include "core/publish_specs.generated.h"
#include "log.h"
#include <cstring>

extern "C" {
uintptr_t g_rs7_publish_ports = 0;
uintptr_t g_rs7_native_publish = 0;
#define PUB_VAR(id)                                                                                                    \
    __declspec(dllexport) uintptr_t g_rs7_pubcopy_##id = 0;                                                            \
    __declspec(dllexport) uintptr_t g_rs7_pubtramp_##id = 0;
PUB_VAR(PUBCL)
PUB_VAR(PUBTO)
PUB_VAR(PUBWU)
#undef PUB_VAR
}

namespace {
template <class T>
T Rd(const uint8_t* p, unsigned off) {
    T v;

    std::memcpy(&v, p + off, sizeof v);

    return v;
}

struct Pair {
    uintptr_t* copy;
    uintptr_t* tramp;
};

const Pair pairs[] = {{&g_rs7_pubcopy_PUBCL, &g_rs7_pubtramp_PUBCL},
                      {&g_rs7_pubcopy_PUBTO, &g_rs7_pubtramp_PUBTO},
                      {&g_rs7_pubcopy_PUBWU, &g_rs7_pubtramp_PUBWU}};

uint8_t* Matcher(uint8_t* tracker) {
    uint8_t* input = tracker ? Rd<uint8_t*>(tracker, 0x4C19C) : nullptr;

    return input ? Rd<uint8_t*>(input, 8) : nullptr;
}

bool Tails(uint8_t* mat) {
    if (!Rs7Pitch_Tailed(mat, 0)) {
        return false;
    }

    uint8_t* det = Rd<uint8_t*>(mat, 4);
    if (!Rs7Pitch_Tailed(det, 1)) {
        return false;
    }

    const unsigned n = Rd<unsigned>(det, rs7p::kDetCount);

    return n >= 6 && n <= 8 && Rd<int>(det, rs7::kDetInstrument) != 2;
}
}  // namespace

namespace rs7pub {
unsigned Missing(unsigned mask) {
    return kPrerequisites & ~mask;
}

bool PortsEnabled(bool requested, unsigned mask) {
    return requested && Missing(mask) == 0;
}

bool PublicationReady() {
    // Every deferred reader of the stage-5 list is ported (docs/ARCHITECTURE.md): pitch 8750C0,
    // 4E58E0, 4E6E90, 875430 (+I1 875504), 8755C0, the 1199D2C callers; FretGrid bounds, GetCellPos,
    // 58E790, 594530, edges, 58CF90; nextgrid NN1/NN3/NN4 (spawn 7A56F0 reads cells through R1 ->
    // PUBGP). Publication still needs the ports built (Rs7Publish_BuildCopies) and, per song, the
    // native owners and prepared grid (publish_state Apply).
    return true;
}

int Classify(uint8_t* r, const int16_t* t, unsigned n, int midi, int initial) {
    if (!r || !t || n < 6 || n > 8) {
        return initial;
    }

    const unsigned original = r[0xC0];
    const int fret = r[0xC1];

    for (unsigned s = 0; s < n; ++s) {
        if (s == original) {
            continue;
        }

        // Unsigned arithmetic reproduces the game's 32-bit ADD/SUB, including wrap.
        const uint32_t expected = uint32_t(g_rs7_pitch_gbase[s]) + uint32_t(int(t[s])) + uint32_t(fret);
        const int32_t delta = int32_t(uint32_t(midi) - expected);
        const int32_t distance = delta < 0 ? int32_t(0u - uint32_t(delta)) : delta;

        if (distance <= 2) {  // signed abs(INT32_MIN) wraps just like CDQ/XOR/SUB in the game
            const int string = int(s);
            const int f = delta == 0 ? fret : int32_t(uint32_t(fret) + uint32_t(delta));
            const int clamped = f < 0 ? 0 : f;

            std::memcpy(r + 0x18, &string, 4);
            std::memcpy(r + 0x14, &clamped, 4);
            return delta == 0 ? 2 : 1;
        }

        if (initial != 7) {
            break;
        }
    }

    return initial;
}
}  // namespace rs7pub

extern "C" void __cdecl Rs7Publish_Configure(int requested) {
    const unsigned mask = (g_rs7_native_strings ? 1u : 0u) | (g_rs7_native_fretgrid ? 2u : 0u) |
                          (g_rs7_native_nextgrid ? 4u : 0u) | (g_rs7_native_pitch ? 8u : 0u) |
                          (g_rs7_native_colours ? 16u : 0u);

    g_rs7_publish_ports = rs7pub::PortsEnabled(requested != 0, mask) ? 1 : 0;
    g_rs7_native_publish = g_rs7_publish_ports && rs7pub::PublicationReady() ? 1 : 0;
    if (!requested) {
        return;
    }

    const char* names[] = {"native_strings", "native_fretgrid", "native_nextgrid", "native_pitch", "native_colours"};

    for (unsigned i = 0; i < 5; ++i) {
        if (rs7pub::Missing(mask) & (1u << i)) {
            Rs7Log("native_publish: OFF - [neck] %s is off or failed initialization", names[i]);
        }
    }

    if (g_rs7_publish_ports) {
        Rs7Log("native_publish: prerequisites met, building the publication ports");
    }
}

extern "C" uintptr_t __cdecl Rs7Publish_Target(unsigned kind, uint8_t* reg) {
    if (kind >= 3) {
        return 0;
    }

    const Pair& p = pairs[kind];
    bool use = false;

    if (g_rs7_publish_ports && *p.copy) {
        if (kind == 0) {
            use = Tails(Matcher(reg));
        } else if (kind == 1) {
            use = Tails(reg);
        } else if (reg) {
            uint8_t* guitar = Rd<uint8_t*>(reg, 0);
            uint8_t* info = guitar ? Rd<uint8_t*>(guitar, 0x24) : nullptr;
            const unsigned n = info ? info[0xB] : 0;

            use = n == 4 || n == 6 || n == 7 || n == 8;
        }
    }

    return use ? *p.copy : *p.tramp;
}

extern "C" int __cdecl Rs7Publish_Classify(uint8_t* tracker, uint8_t* r, int midi, int initial) {
    uint8_t* mat = Matcher(tracker);
    uint8_t* det = Rd<uint8_t*>(mat, 4);  // entry guard verified both tails

    return rs7pub::Classify(r, reinterpret_cast<const int16_t*>(mat + rs7p::kMatTuning8),
                            Rd<unsigned>(det, rs7p::kDetCount), midi, initial);
}

bool Rs7Publish_BuildCopies() {
    for (int i = 0; i < kRs7PubSpecCount; ++i) {
        const auto& sp = kRs7PubSpecs[i];
        char why[160] = {};

        if (!Rs7Fn_Build(sp, sp.va, pairs[i].copy, pairs[i].tramp, why, sizeof why)) {
            Rs7Log("native_publish: OFF - %s", why);
            g_rs7_publish_ports = g_rs7_native_publish = 0;
            return false;
        }
    }

    return true;
}
