#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diagrams/diagrams.h"
#include "features/publish/publish.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

namespace {
uint32_t ExpectedGates(const char* id) {
    uint32_t gates = 0;

    if (!std::strncmp(id, "GK", 2)) {
        gates |= kRsxrGateClite;
    }
    if (!std::strcmp(id, "PSQ1")) {
        gates |= kRsxrGateNoClite;
    }
    if (!std::strncmp(id, "LS", 2) && std::strlen(id) == 3) {
        gates |= kRsxrGatePublishTrace;
    }
    if (!std::strcmp(id, "FGRB") || !std::strcmp(id, "NN1") || !std::strcmp(id, "NN3") || !std::strcmp(id, "NN4")) {
        gates |= kRsxrGateNoPublish;
    }
    if (!std::strncmp(id, "CL", 2)) {
        gates |= kRsxrGateChordloops;
    }
    if (!std::strncmp(id, "CM", 2) || !std::strncmp(id, "CB", 2)) {
        gates |= kRsxrGateColours;
    }
    if (!std::strncmp(id, "TV", 2)) {
        gates |= kRsxrGateTestVariant;
    }
    if (!std::strcmp(id, "GLW")) {
        gates |= kRsxrGateGlow;
    }
    if (!std::strncmp(id, "DG", 2) || !std::strcmp(id, "TP13")) {
        gates |= kRsxrGateDiagrams;
    }

    return gates;
}
}  // namespace

TEST(registration_metadata_preserves_all_previous_name_conditions) {
    constexpr uint32_t migrated = kRsxrGateClite | kRsxrGateNoClite | kRsxrGatePublishTrace | kRsxrGateNoPublish |
                                  kRsxrGateChordloops | kRsxrGateColours | kRsxrGateTestVariant | kRsxrGateGlow |
                                  kRsxrGateDiagrams;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);

        CHECK(h != nullptr);
        if (h) {
            CHECK_EQ(h->gates & migrated, ExpectedGates(h->id));
        }
    }
}

TEST(registration_gates_ignore_site_names) {
    rsxrtest::Restore clite(g_rsxr_clite), trace(g_rsxr_publish_trace), publish(g_rsxr_publish_ports);
    rsxrtest::Restore chords(g_rsxr_native_chordloops), colours(g_rsxr_native_colours);
    rsxrtest::Restore glow(g_rsxr_glow_fix), diagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();

    for (int switches = 0; switches < 128; ++switches) {
        g_rsxr_clite = bool(switches & 1);
        g_rsxr_publish_trace = bool(switches & 2);
        g_rsxr_publish_ports = bool(switches & 4);
        g_rsxr_native_chordloops = bool(switches & 8);
        g_rsxr_native_colours = bool(switches & 16);
        g_rsxr_glow_fix = bool(switches & 32);
        g_rsxr_diagrams = bool(switches & 64);

        for (const char* id : {"GKC", "PSQ1", "LSA", "FGRB", "NN1", "NN3", "NN4", "CLB0", "CMUV", "CBUV", "TVCB", "GLW",
                               "DGM", "TP13"}) {
            const RsxrHook* h = Rsxr_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            RsxrHook renamed = *h;

            renamed.id = "renamed";

            for (const bool strings : {false, true}) {
                CHECK_EQ(Rsxr_ShouldPatch(*h, true, true, true, strings),
                         Rsxr_ShouldPatch(renamed, true, true, true, strings));
            }
        }
    }
}
