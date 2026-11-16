// The groups' failure state, the unit cascade and the write conditions (hook_rules.h): which site Rs7_Startup
// writes, given the features' switches.
#include "hook_rules.h"
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diagrams/diagrams.h"
#include "features/fretgrid/fretgrid.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/menuload/menuload.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/psarc/psarc.h"
#include "features/publish/publish.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "features/testdrive/testdrive.h"
#include "features/tuner/tuner.h"
#include "model/gates.h"

namespace {
bool g_featureOff[size_t(Rs7Feature::Count)] = {};

// The runtime switches a group's failure clears (read by its stubs / callbacks); a group without a row has none.
struct GroupSwitches {
    Rs7Feature group;
    uintptr_t* a;
    uintptr_t* b;  // or nullptr
};

const GroupSwitches kGroupSwitches[] = {
    {Rs7Feature::Colours, &g_rs7_native_colours, nullptr},
    {Rs7Feature::Pegs, &g_rs7_native_pegset, nullptr},
    {Rs7Feature::Strings, &g_rs7_native_strings, nullptr},
    {Rs7Feature::Fretgrid, &g_rs7_native_fretgrid, nullptr},
    {Rs7Feature::Pitch, &g_rs7_native_pitch, nullptr},
    {Rs7Feature::Nextgrid, &g_rs7_native_nextgrid, nullptr},
    {Rs7Feature::Diagrams, &g_rs7_diagrams, nullptr},
    {Rs7Feature::Publish, &g_rs7_publish_ports, &g_rs7_native_publish},
    {Rs7Feature::Chords, &g_rs7_native_chordloops, nullptr},
    {Rs7Feature::Tuner, &g_rs7_native_tuner, nullptr},
    {Rs7Feature::Clite, &g_rs7_clite, nullptr},
    {Rs7Feature::Glow, &g_rs7_glow_fix, nullptr},
    {Rs7Feature::MenuLoad, &g_rs7_menu_retry, nullptr},
};

// Marks one group failed and clears its switches (no cascade).
void MarkOff(Rs7Feature f) {
    g_featureOff[size_t(f)] = true;

    for (const GroupSwitches& g : kGroupSwitches) {
        if (g.group != f) {
            continue;
        }

        *g.a = 0;

        if (g.b) {
            *g.b = 0;
        }
    }
}
}  // namespace

bool Rs7_FeatureDisabled(Rs7Feature f) {
    return size_t(f) < size_t(Rs7Feature::Count) && g_featureOff[size_t(f)];
}

void Rs7_ResetFeatureFailures() {
    for (auto& b : g_featureOff) {
        b = false;
    }
}

bool Rs7_DisableFeature(Rs7Feature f) {
    if (f == Rs7Feature::Core || size_t(f) >= size_t(Rs7Feature::Count)) {
        return false;
    }

    MarkOff(f);

    // Native 7-string mode is both halves: C-lite (the guitar is built with the count) and the native_publish
    // ports (the layouts that can show it). One half failing takes the other with it: a half cannot stand alone.
    if (f == Rs7Feature::Clite && g_rs7_publish_ports) {
        MarkOff(Rs7Feature::Publish);
    }

    if (f == Rs7Feature::Publish && g_rs7_clite) {
        MarkOff(Rs7Feature::Clite);
    }

    // The native tuner session carries copies of the GuitarStrings / FretGrid / colour edits: without any of them it
    // must go too (its switch only; the Tuner group itself stays).
    if (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours) {
        g_rs7_native_tuner = 0;
    }

    // A guitar built with 7 needs the native PegSet: GKC writes peg request 7/8 and the stock
    // Rnd_PegSet_Load 0x59C4D0 would loop over it on the six-slot stock layout. Any loss of Pegs (startup config or a
    // runtime isolation after the initial configuration) takes clite and the ports with it before the next round.
    if (!g_rs7_native_pegset && (g_rs7_clite || g_rs7_publish_ports)) {
        MarkOff(Rs7Feature::Clite);
        MarkOff(Rs7Feature::Publish);
    }

    // A guitar built with 7 needs the native 8-slot tuner session (docs/ARCHITECTURE.md): the stock
    // session's per-string arrays have six slots and its loops run to info+0Bh. clite goes, and native_publish with it.
    if (!g_rs7_native_tuner && g_rs7_clite) {
        MarkOff(Rs7Feature::Clite);

        if (g_rs7_publish_ports) {
            MarkOff(Rs7Feature::Publish);
        }
    }

    // native_publish needs all five native layouts (rs7pub::PortsEnabled); chordloops needs native_strings.
    if (g_rs7_publish_ports && (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours ||
                                !g_rs7_native_pitch || !g_rs7_native_nextgrid)) {
        MarkOff(Rs7Feature::Publish);

        if (g_rs7_clite) {  // ... and without the ports a guitar built with 7 strings has nothing to show it
            MarkOff(Rs7Feature::Clite);
        }
    }

    if (g_rs7_native_chordloops && !g_rs7_native_strings) {
        MarkOff(Rs7Feature::Chords);
    }

    return true;
}

// Parameters are switches the tests vary; other switches are owned by their features. Every
// write condition comes from registration metadata, so site renames do not alter patch selection.
bool Rs7_ShouldPatch(const Rs7Hook& h, bool diagnostics, bool privateOn, bool nativePegs, bool nativeStrings) {
    if (g_featureOff[size_t(Rs7_FeatureOf(h))]) {
        return false;
    }
    if ((h.gates & kRs7GateMenuLoad) && !g_rs7_menu_retry) {
        return false;
    }
    if ((h.gates & kRs7GateTuner) &&
        (!g_rs7_native_tuner || !nativeStrings || !g_rs7_native_fretgrid || !g_rs7_native_colours)) {
        return false;
    }
    if ((h.gates & kRs7GatePublish) && !g_rs7_publish_ports) {
        return false;
    }
    if ((h.gates & kRs7GateClite) && !g_rs7_clite) {
        return false;
    }
    if ((h.gates & kRs7GateNoClite) && g_rs7_clite) {
        return false;
    }
    if ((h.gates & kRs7GatePublishTrace) && !g_rs7_publish_trace) {
        return false;
    }
    if ((h.gates & kRs7GateNoPublish) && g_rs7_publish_ports) {
        return false;
    }
    if ((h.gates & kRs7GateChordloops) && (!g_rs7_native_chordloops || !nativeStrings)) {
        return false;
    }
    if ((h.gates & kRs7GateColours) && !g_rs7_native_colours) {
        return false;
    }
    if ((h.gates & kRs7GateTestVariant) && !Rs7Variant_Get().Any()) {
        return false;
    }
    if ((h.gates & kRs7GateGlow) && !g_rs7_glow_fix) {
        return false;
    }
    if ((h.gates & kRs7GateDiagrams) && !g_rs7_diagrams) {
        return false;
    }
    if ((h.gates & kRs7GatePegs) && !nativePegs) {
        return false;
    }
    if ((h.gates & kRs7GateStrings) && !nativeStrings) {
        return false;
    }
    if ((h.gates & kRs7GateFretgrid) && !g_rs7_native_fretgrid) {
        return false;
    }
    if ((h.gates & kRs7GatePitch) && !g_rs7_native_pitch) {
        return false;
    }
    if ((h.gates & kRs7GateNextgrid) && !g_rs7_native_nextgrid) {
        return false;
    }

    return !h.ref && (diagnostics || !h.diagOnly) && (privateOn || !h.privateOnly);
}
