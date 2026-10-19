// The publication switches, startup, and the three preparation copies (PUBCL PUBTO PUBWU): built from the live code
// (tools/gen_publish.py), chosen per call by Rs7Publish_Target (entry mid-hooks in hooks_entries.cpp, the calls written
// into them in hooks_callin.cpp).
#include "features/publish/hooks.h"
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish_specs.generated.h"
#include "features/strings/strings.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include <windows.h>
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

void Rs7Publish_ReadIni(const wchar_t* ini) {
    g_rs7_publish_trace = GetPrivateProfileIntW(L"debug", L"publish_trace", 0, ini) != 0;
}

void Rs7Publish_Start(bool requested) {
    Rs7Publish_Configure(requested);
    if (g_rs7_publish_ports && (!Rs7Publish_BuildCopies() || !Rs7PublishPitch_BuildCopies())) {
        g_rs7_publish_ports = g_rs7_native_publish = 0;
    }
    if (g_rs7_native_publish) {
        Rs7Log("native_publish: ON - native 8-slot layouts and ports; a guitar shows the string count it was built "
               "with (C-lite)");
    }
}

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
