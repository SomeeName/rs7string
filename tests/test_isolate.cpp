#include "core/colournative.h"
#include "core/diagrams.h"
#include "core/fretgrid.h"
#include "core/gsnative.h"
#include "core/nextgrid.h"
#include "core/pitch8.h"
#include "core/publish.h"
#include "core/resolve.h"
#include "core/tuner8.h"
#include "generated/sites.generated.h"
#include "hooks/isolate.h"
#include "test.h"
#include <cstring>

namespace {
struct Fixture {
    // CMINIT / CMBUILD -> Colours, DGM -> Diagrams, R1 -> Core (real registry ids).
    const char* ids[4] = {"R2", "CMINIT", "CMBUILD", "DGM"};
    bool written[4] = {};
    bool failPrepare[4] = {}, failApply[4] = {};
    int restored = 0;
    std::vector<std::string> log;

    Rs7PatchOps Ops(const Rs7Hook** hooks) {
        Rs7PatchOps o;

        o.count = 4;
        o.feature = [=](size_t i) {
            return Rs7_FeatureOf(*hooks[i]);
        };
        o.name = [this](size_t i) {
            return ids[i];
        };
        o.should = [=](size_t i) {
            return Rs7_ShouldPatch(*hooks[i], false, false, false, false);
        };
        o.prepare = [this](size_t i) {
            return !failPrepare[i];
        };
        o.apply = [this](size_t i) {
            if (failApply[i]) {
                return false;
            }

            written[i] = true;

            return true;
        };
        o.restore = [this](size_t i) {
            written[i] = false;
            ++restored;

            return true;
        };
        o.log = [this](const std::string& m) {
            log.push_back(m);
        };

        return o;
    }
};
}  // namespace

TEST(isolation_failing_colour_site_is_a_unit_failure_and_aborts) {
    // 1.0: native colours have no legacy fallback; a colour site failing leaves the game stock (nothing written).
    const auto saved = g_rs7_native_colours, savedDg = g_rs7_diagrams;

    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = 1;
    g_rs7_diagrams = 1;
    Fixture f;
    const Rs7Hook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rs7_FindHook(f.ids[i]);
        CHECK(hooks[i] != nullptr);
        if (!hooks[i]) {
            return;
        }
    }

    CHECK(Rs7_FeatureOf(*hooks[0]) == Rs7Feature::Core);
    CHECK(Rs7_FeatureOf(*hooks[1]) == Rs7Feature::Colours);
    CHECK(Rs7_FeatureOf(*hooks[3]) == Rs7Feature::Diagrams);
    CHECK(Rs7_IsSevenUnit(Rs7Feature::Colours) && !Rs7_IsSevenUnit(Rs7Feature::Diagrams));
    f.failPrepare[2] = true;  // CMBUILD cannot be relocated
    std::vector<size_t> written;

    CHECK(Rs7_PatchIsolated(f.Ops(hooks), written) == Rs7PatchResult::CoreFailed);
    CHECK(!f.written[0] && !f.written[1] && !f.written[2] && !f.written[3]);
    bool named = false;

    for (auto& l : f.log) {
        named |= l.find("CMBUILD") != std::string::npos && l.find("native 7-string unit") != std::string::npos;
    }

    CHECK(named);
    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = saved;
    g_rs7_diagrams = savedDg;
}

TEST(isolation_failing_diagram_site_disables_only_diagrams) {
    const auto saved = g_rs7_native_colours, savedDg = g_rs7_diagrams;

    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = 1;
    g_rs7_diagrams = 1;
    Fixture f;
    const Rs7Hook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rs7_FindHook(f.ids[i]);
    }

    f.failApply[3] = true;  // the diagram site's write fails after the others were written
    std::vector<size_t> written;

    CHECK(Rs7_PatchIsolated(f.Ops(hooks), written) == Rs7PatchResult::Ok);
    CHECK(f.restored >= 3);  // the round is undone, then rewritten without Diagrams
    CHECK(f.written[0] && f.written[1] && f.written[2] && !f.written[3]);
    CHECK(Rs7_FeatureDisabled(Rs7Feature::Diagrams) && g_rs7_diagrams == 0);
    CHECK(!Rs7_FeatureDisabled(Rs7Feature::Colours) && g_rs7_native_colours == 1);
    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = saved;
    g_rs7_diagrams = savedDg;
}

TEST(isolation_core_failure_still_aborts_and_restores) {
    const auto saved = g_rs7_native_colours;

    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = 1;
    Fixture f;
    const Rs7Hook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rs7_FindHook(f.ids[i]);
    }

    f.failApply[3] = false;
    f.failApply[0] = false;
    f.failPrepare[0] = true;  // R1 is core
    std::vector<size_t> written;

    CHECK(Rs7_PatchIsolated(f.Ops(hooks), written) == Rs7PatchResult::CoreFailed);
    CHECK(!f.written[0] && !f.written[1] && !f.written[2] && !f.written[3]);
    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = saved;
}

TEST(isolation_resolve_failure_of_a_colour_site_aborts) {
    const auto saved = g_rs7_native_colours, savedDg = g_rs7_diagrams;

    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = 1;
    g_rs7_diagrams = 1;
    const Rs7Site* cm = nullptr;
    const Rs7Site* core = nullptr;
    const Rs7Site* dg = nullptr;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        if (!std::strcmp(kRs7Sites[i].id, "CMUV")) {
            cm = &kRs7Sites[i];
        }
        if (!std::strcmp(kRs7Sites[i].id, "R2")) {
            core = &kRs7Sites[i];
        }
        if (!std::strcmp(kRs7Sites[i].id, "DGM")) {
            dg = &kRs7Sites[i];
        }
    }

    CHECK(cm && core && dg);
    if (!cm || !core || !dg) {
        return;
    }

    Rs7ResolveResult res{false, {}, {"CMUV: 0 matches (need 1)"}, {Rs7Resolved{core, 0x401000, 0, {}}}, {cm}};
    std::vector<std::string> log;

    CHECK(!Rs7_IsolateResolveFailures(res, [&](const std::string& m) { log.push_back(m); }));
    CHECK(!log.empty() && log[0].find("CMUV") != std::string::npos);
    Rs7_ResetFeatureFailures();
    Rs7ResolveResult other{false, {}, {"DGM: 0 matches (need 1)"}, {Rs7Resolved{core, 0x401000, 0, {}}}, {dg}};

    CHECK(Rs7_IsolateResolveFailures(other, [](const std::string&) {}));
    CHECK(other.ok && other.sites.size() == 1);
    CHECK(Rs7_FeatureDisabled(Rs7Feature::Diagrams) && !Rs7_FeatureDisabled(Rs7Feature::Colours));
    Rs7_ResetFeatureFailures();
    Rs7ResolveResult bad{false, {}, {"R1: 0 matches (need 1)"}, {}, {core}};

    CHECK(!Rs7_IsolateResolveFailures(bad, [](const std::string&) {}));
    Rs7_ResetFeatureFailures();
    g_rs7_native_colours = saved;
    g_rs7_diagrams = savedDg;
}

TEST(publish_and_chordloops_sites_have_their_own_groups) {
    // A PUB* site is Publish, a CL* site is Chords, a pitch site is Pitch: no new site falls into Core,
    // so a failing site disables only its own group.
    size_t pub = 0, cl = 0;

    for (size_t i = 0; i < sizeof(kRs7Sites) / sizeof(kRs7Sites[0]); ++i) {
        const char* id = kRs7Sites[i].id;
        const bool isPub = !std::strncmp(id, "PUB", 3), isCl = !std::strncmp(id, "CL", 2);
        if (!isPub && !isCl) {
            continue;
        }

        const Rs7Hook* h = Rs7_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        if (isPub) {
            ++pub;
            CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Publish);
        } else {
            ++cl;
            CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Chords);
        }
    }

    CHECK_EQ(pub, 25u);
    CHECK_EQ(cl, 19u);

    for (const char* id : {"PA1", "PA2", "PM0", "PR0", "PT1", "PM1", "PE0", "PC1", "PC2", "PC3", "PC4"}) {
        const Rs7Hook* h = Rs7_FindHook(id);

        CHECK(h != nullptr);
        if (h) {
            CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Pitch);
        }
    }
}

TEST(tuner8_sites_have_their_own_group) {
    // Every TU8* / TS8* site (session copies, mid-hooks) is Tuner; disabling it clears native_tuner only.
    size_t n = 0;

    for (size_t i = 0; i < sizeof(kRs7Sites) / sizeof(kRs7Sites[0]); ++i) {
        const char* id = kRs7Sites[i].id;
        if (std::strncmp(id, "TU8", 3) && std::strncmp(id, "TS8", 3)) {
            continue;
        }

        const Rs7Hook* h = Rs7_FindHook(id);

        CHECK(h != nullptr);

        if (h) {
            ++n;
            CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Tuner);
        }
    }

    CHECK_EQ(n, 37u);
    Rs7_ResetFeatureFailures();
    const uintptr_t savedT = g_rs7_native_tuner, savedC = g_rs7_native_colours;

    g_rs7_native_tuner = 1;
    g_rs7_native_colours = 1;
    CHECK(Rs7_DisableFeature(Rs7Feature::Tuner));
    CHECK(g_rs7_native_tuner == 0 && g_rs7_native_colours == 1);
    Rs7_ResetFeatureFailures();
    g_rs7_native_tuner = 1;
    CHECK(Rs7_DisableFeature(Rs7Feature::Colours));  // a prerequisite of the tuner copies goes: so does the tuner
    CHECK(g_rs7_native_tuner == 0);
    Rs7_ResetFeatureFailures();
    g_rs7_native_tuner = savedT;
    g_rs7_native_colours = savedC;
}

TEST(fretgrid_failure_takes_native_publish_and_native_tuner_with_it) {
    // The deferred FretGrid layout (game thread) failing disables Fretgrid; publish and the tuner copies need it.
    Rs7_ResetFeatureFailures();
    const uintptr_t sT = g_rs7_native_tuner, sP = g_rs7_publish_ports, sN = g_rs7_native_publish,
                    sS = g_rs7_native_strings, sF = g_rs7_native_fretgrid, sC = g_rs7_native_colours,
                    sI = g_rs7_native_pitch, sG = g_rs7_native_nextgrid;

    g_rs7_native_tuner = g_rs7_publish_ports = g_rs7_native_publish = g_rs7_native_strings = g_rs7_native_fretgrid =
        g_rs7_native_colours = g_rs7_native_pitch = g_rs7_native_nextgrid = 1;
    CHECK(Rs7_DisableFeature(Rs7Feature::Fretgrid));
    CHECK(g_rs7_native_fretgrid == 0 && g_rs7_native_tuner == 0 && g_rs7_publish_ports == 0 &&
          g_rs7_native_publish == 0);
    CHECK(Rs7_FeatureDisabled(Rs7Feature::Publish) && !Rs7_FeatureDisabled(Rs7Feature::Strings));
    Rs7_ResetFeatureFailures();
    g_rs7_native_tuner = sT;
    g_rs7_publish_ports = sP;
    g_rs7_native_publish = sN;
    g_rs7_native_strings = sS;
    g_rs7_native_fretgrid = sF;
    g_rs7_native_colours = sC;
    g_rs7_native_pitch = sI;
    g_rs7_native_nextgrid = sG;
}
