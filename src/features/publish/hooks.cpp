// The publication switches, startup, and the three preparation copies (PUBCL PUBTO PUBWU): built from the live code
// (tools/gen_publish.py), chosen per call by RsxrPublish_Target (entry mid-hooks in hooks_entries.cpp, the calls
// written into them in hooks_callin.cpp).
#include "features/publish/hooks.h"
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish_specs.generated.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

extern "C" {
uintptr_t g_rsxr_publish_ports = 0;
uintptr_t g_rsxr_native_publish = 0;
#define PUB_VAR(id)                                                                                                    \
    __declspec(dllexport) uintptr_t g_rsxr_pubcopy_##id = 0;                                                           \
    __declspec(dllexport) uintptr_t g_rsxr_pubtramp_##id = 0;
PUB_VAR(PUBCL)
PUB_VAR(PUBTO)
PUB_VAR(PUBWU)
#undef PUB_VAR
}

namespace {
using rsxrpubhooks::Pair;
using rsxrpubhooks::UncheckedRead;

const Pair pairs[] = {{&g_rsxr_pubcopy_PUBCL, &g_rsxr_pubtramp_PUBCL},
                      {&g_rsxr_pubcopy_PUBTO, &g_rsxr_pubtramp_PUBTO},
                      {&g_rsxr_pubcopy_PUBWU, &g_rsxr_pubtramp_PUBWU}};

uint8_t* Matcher(uint8_t* tracker) {
    uint8_t* input = tracker ? UncheckedRead<uint8_t*>(tracker, rsxrpubhooks::kTrackerInput) : nullptr;

    return input ? UncheckedRead<uint8_t*>(input, rsxr::kInputMatcher) : nullptr;
}

bool Tails(uint8_t* mat) {
    if (!RsxrPitch_Tailed(mat, 0)) {
        return false;
    }

    uint8_t* det = UncheckedRead<uint8_t*>(mat, rsxr::kMatcherDetector);
    if (!RsxrPitch_Tailed(det, 1)) {
        return false;
    }

    const unsigned n = UncheckedRead<unsigned>(det, rsxrp::kDetCount);

    return n >= 6 && n <= 8 && !rsxr::IsBassKind(UncheckedRead<int>(det, rsxr::kDetInstrument));
}
}  // namespace

void RsxrPublish_ReadIni(const wchar_t* ini) {
    g_rsxr_publish_trace = GetPrivateProfileIntW(L"debug", L"publish_trace", 0, ini) != 0;
}

void RsxrPublish_Start(bool requested) {
    RsxrPublish_Configure(requested);
    if (g_rsxr_publish_ports && (!RsxrPublish_BuildCopies() || !RsxrPublishPitch_BuildCopies())) {
        g_rsxr_publish_ports = g_rsxr_native_publish = 0;
    }
    if (g_rsxr_native_publish) {
        RsxrLog("native_publish: ON - native 8-slot layouts and ports; a guitar shows the string count it was built "
                "with (C-lite)");
    }
}

extern "C" void __cdecl RsxrPublish_Configure(int requested) {
    const unsigned mask = (g_rsxr_native_strings ? 1u : 0u) | (g_rsxr_native_fretgrid ? 2u : 0u) |
                          (g_rsxr_native_nextgrid ? 4u : 0u) | (g_rsxr_native_pitch ? 8u : 0u) |
                          (g_rsxr_native_colours ? 16u : 0u);

    g_rsxr_publish_ports = rsxrpub::PortsEnabled(requested != 0, mask) ? 1 : 0;
    g_rsxr_native_publish = g_rsxr_publish_ports && rsxrpub::PublicationReady() ? 1 : 0;
    if (!requested) {
        return;
    }

    const char* names[] = {"native_strings", "native_fretgrid", "native_nextgrid", "native_pitch", "native_colours"};

    for (unsigned i = 0; i < 5; ++i) {
        if (rsxrpub::Missing(mask) & (1u << i)) {
            RsxrLog("native_publish: OFF - [neck] %s is off or failed initialization", names[i]);
        }
    }

    if (g_rsxr_publish_ports) {
        RsxrLog("native_publish: prerequisites met, building the publication ports");
    }
}

extern "C" uintptr_t __cdecl RsxrPublish_Target(unsigned kind, uint8_t* reg) {
    if (kind >= 3) {
        return 0;
    }

    const Pair& p = pairs[kind];
    bool use = false;

    if (g_rsxr_publish_ports && *p.copy) {
        if (kind == 0) {
            use = Tails(Matcher(reg));
        } else if (kind == 1) {
            use = Tails(reg);
        } else if (reg) {
            uint8_t* guitar = UncheckedRead<uint8_t*>(reg, 0);
            uint8_t* info = rsxr::GuitarInfo(guitar);
            const unsigned n = info ? info[rsxr::kInfoStrings] : 0;

            use = n >= 4 && n <= 8;  // every built count: stock bass and guitar, extended bass 5 / 6, guitar 7 / 8
        }
    }

    return use ? *p.copy : *p.tramp;
}

extern "C" int __cdecl RsxrPublish_Classify(uint8_t* tracker, uint8_t* r, int midi, int initial) {
    uint8_t* mat = Matcher(tracker);
    uint8_t* det = UncheckedRead<uint8_t*>(mat, rsxr::kMatcherDetector);  // entry guard verified both tails

    return rsxrpub::Classify(r, reinterpret_cast<const int16_t*>(mat + rsxrp::kMatTuning8),
                             UncheckedRead<unsigned>(det, rsxrp::kDetCount), midi, initial);
}

bool RsxrPublish_BuildCopies() {
    for (int i = 0; i < kRsxrPubSpecCount; ++i) {
        const auto& sp = kRsxrPubSpecs[i];
        char why[160] = {};

        if (!RsxrFn_Build(sp, sp.va, pairs[i].copy, pairs[i].tramp, why, sizeof why)) {
            RsxrLog("native_publish: OFF - %s", why);
            g_rsxr_publish_ports = g_rsxr_native_publish = 0;
            return false;
        }
    }

    return true;
}
