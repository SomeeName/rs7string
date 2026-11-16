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
        gates |= kRs7GateClite;
    }
    if (!std::strcmp(id, "PSQ1")) {
        gates |= kRs7GateNoClite;
    }
    if (!std::strncmp(id, "LS", 2) && std::strlen(id) == 3) {
        gates |= kRs7GatePublishTrace;
    }
    if (!std::strcmp(id, "FGRB") || !std::strcmp(id, "NN1") || !std::strcmp(id, "NN3") || !std::strcmp(id, "NN4")) {
        gates |= kRs7GateNoPublish;
    }
    if (!std::strncmp(id, "CL", 2)) {
        gates |= kRs7GateChordloops;
    }
    if (!std::strncmp(id, "CM", 2) || !std::strncmp(id, "CB", 2)) {
        gates |= kRs7GateColours;
    }
    if (!std::strncmp(id, "TV", 2)) {
        gates |= kRs7GateTestVariant;
    }
    if (!std::strcmp(id, "GLW")) {
        gates |= kRs7GateGlow;
    }
    if (!std::strncmp(id, "DG", 2) || !std::strcmp(id, "TP13")) {
        gates |= kRs7GateDiagrams;
    }

    return gates;
}
}  // namespace

TEST(registration_metadata_preserves_all_previous_name_conditions) {
    constexpr uint32_t migrated = kRs7GateClite | kRs7GateNoClite | kRs7GatePublishTrace | kRs7GateNoPublish |
                                  kRs7GateChordloops | kRs7GateColours | kRs7GateTestVariant | kRs7GateGlow |
                                  kRs7GateDiagrams;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);

        CHECK(h != nullptr);
        if (h) {
            CHECK_EQ(h->gates & migrated, ExpectedGates(h->id));
        }
    }
}

TEST(registration_gates_ignore_site_names) {
    rs7test::Restore clite(g_rs7_clite), trace(g_rs7_publish_trace), publish(g_rs7_publish_ports);
    rs7test::Restore chords(g_rs7_native_chordloops), colours(g_rs7_native_colours);
    rs7test::Restore glow(g_rs7_glow_fix), diagrams(g_rs7_diagrams);

    Rs7_ResetFeatureFailures();

    for (int switches = 0; switches < 128; ++switches) {
        g_rs7_clite = bool(switches & 1);
        g_rs7_publish_trace = bool(switches & 2);
        g_rs7_publish_ports = bool(switches & 4);
        g_rs7_native_chordloops = bool(switches & 8);
        g_rs7_native_colours = bool(switches & 16);
        g_rs7_glow_fix = bool(switches & 32);
        g_rs7_diagrams = bool(switches & 64);

        for (const char* id : {"GKC", "PSQ1", "LSA", "FGRB", "NN1", "NN3", "NN4", "CLB0", "CMUV", "CBUV", "TVCB", "GLW",
                               "DGM", "TP13"}) {
            const Rs7Hook* h = Rs7_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            Rs7Hook renamed = *h;

            renamed.id = "renamed";

            for (const bool strings : {false, true}) {
                CHECK_EQ(Rs7_ShouldPatch(*h, true, true, true, strings),
                         Rs7_ShouldPatch(renamed, true, true, true, strings));
            }
        }
    }
}
