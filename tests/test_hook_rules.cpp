// The unit cascade of Rs7_DisableFeature (src/hook_rules.cpp), pinned for every group: from the state after a
// complete startup (every switch on), which groups end up failed and which runtime switches end up cleared.
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diagrams/diagrams.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "features/tuner/tuner.h"
#include "hook_rules.h"
#include "test.h"
#include <cstdio>

namespace {
using F = Rs7Feature;

// The runtime switches the cascade reads or clears, in this order.
uintptr_t* const kSwitches[] = {&g_rs7_native_colours,    &g_rs7_native_pegset, &g_rs7_native_strings,
                                &g_rs7_native_fretgrid,   &g_rs7_native_pitch,  &g_rs7_native_nextgrid,
                                &g_rs7_diagrams,          &g_rs7_publish_ports, &g_rs7_native_publish,
                                &g_rs7_native_chordloops, &g_rs7_native_tuner,  &g_rs7_clite,
                                &g_rs7_glow_fix};
constexpr size_t kSwitchCount = sizeof kSwitches / sizeof kSwitches[0];

// Bit i: kSwitches[i] cleared.
enum : unsigned {
    sColours = 1u << 0,
    sPegs = 1u << 1,
    sStrings = 1u << 2,
    sFretgrid = 1u << 3,
    sPitch = 1u << 4,
    sNextgrid = 1u << 5,
    sDiagrams = 1u << 6,
    sPorts = 1u << 7,
    sPublish = 1u << 8,
    sChords = 1u << 9,
    sTuner = 1u << 10,
    sClite = 1u << 11,
    sGlow = 1u << 12,
};

constexpr unsigned G(F f) {
    return 1u << unsigned(f);
}

struct Expect {
    F group;
    bool accepted;     // Rs7_DisableFeature's result
    unsigned failed;   // G(...) of every failed group afterwards
    unsigned cleared;  // s... of every cleared switch afterwards
};

constexpr unsigned kUnit = G(F::Clite) | G(F::Publish);  // the two halves of native 7-string mode
constexpr unsigned kUnitOff = sClite | sPorts | sPublish;

const Expect kCascade[] = {
    {F::Core, false, 0, 0},
    {F::Colours, true, G(F::Colours) | kUnit, sColours | sTuner | kUnitOff},
    {F::Pegs, true, G(F::Pegs) | kUnit, sPegs | kUnitOff},
    {F::Strings, true, G(F::Strings) | kUnit | G(F::Chords), sStrings | sTuner | kUnitOff | sChords},
    {F::Fretgrid, true, G(F::Fretgrid) | kUnit, sFretgrid | sTuner | kUnitOff},
    {F::Pitch, true, G(F::Pitch) | kUnit, sPitch | kUnitOff},
    {F::Nextgrid, true, G(F::Nextgrid) | kUnit, sNextgrid | kUnitOff},
    {F::Diagrams, true, G(F::Diagrams), sDiagrams},
    {F::Vguitar, true, G(F::Vguitar), 0},
    {F::Diag, true, G(F::Diag), 0},
    {F::Publish, true, kUnit, kUnitOff},
    {F::Chords, true, G(F::Chords), sChords},
    {F::Tuner, true, G(F::Tuner) | kUnit, sTuner | kUnitOff},
    {F::Psarc, true, G(F::Psarc), 0},
    {F::Clite, true, kUnit, kUnitOff},
    {F::Glow, true, G(F::Glow), sGlow},
    {F::Count, false, 0, 0},
};

unsigned FailedGroups() {
    unsigned m = 0;

    for (unsigned g = 0; g < unsigned(F::Count); ++g) {
        if (Rs7_FeatureDisabled(F(g))) {
            m |= 1u << g;
        }
    }

    return m;
}

unsigned ClearedSwitches() {
    unsigned m = 0;

    for (size_t i = 0; i < kSwitchCount; ++i) {
        if (*kSwitches[i] == 0) {
            m |= 1u << i;
        }
    }

    return m;
}
}  // namespace

TEST(disable_feature_cascade_pinned_for_every_group) {
    static_assert(sizeof kCascade / sizeof kCascade[0] == size_t(F::Count) + 1, "one row per group, plus Count");
    uintptr_t saved[kSwitchCount];

    for (size_t i = 0; i < kSwitchCount; ++i) {
        saved[i] = *kSwitches[i];
    }

    for (const Expect& e : kCascade) {
        Rs7_ResetFeatureFailures();

        for (uintptr_t* s : kSwitches) {
            *s = 1;
        }

        const bool accepted = Rs7_DisableFeature(e.group);
        const unsigned failed = FailedGroups(), cleared = ClearedSwitches();

        if (accepted != e.accepted || failed != e.failed || cleared != e.cleared) {
            std::printf("  group %u: accepted %d failed %04X cleared %04X (expected %d %04X %04X)\n", unsigned(e.group),
                        int(accepted), failed, cleared, int(e.accepted), e.failed, e.cleared);
        }
        CHECK(accepted == e.accepted);
        CHECK(failed == e.failed);
        CHECK(cleared == e.cleared);
    }

    Rs7_ResetFeatureFailures();

    for (size_t i = 0; i < kSwitchCount; ++i) {
        *kSwitches[i] = saved[i];
    }
}
