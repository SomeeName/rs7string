// The groups' failure state, the unit cascade and the write conditions (hook_rules.h): which site Rsxr_Startup
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
bool g_featureOff[size_t(RsxrFeature::Count)] = {};

// The runtime switches a group's failure clears (read by its stubs / callbacks); a group without a row has none.
struct GroupSwitches {
    RsxrFeature group;
    uintptr_t* a;
    uintptr_t* b;  // or nullptr
};

const GroupSwitches kGroupSwitches[] = {
    {RsxrFeature::Colours, &g_rsxr_native_colours, nullptr},
    {RsxrFeature::Pegs, &g_rsxr_native_pegset, nullptr},
    {RsxrFeature::Strings, &g_rsxr_native_strings, nullptr},
    {RsxrFeature::Fretgrid, &g_rsxr_native_fretgrid, nullptr},
    {RsxrFeature::Pitch, &g_rsxr_native_pitch, nullptr},
    {RsxrFeature::Nextgrid, &g_rsxr_native_nextgrid, nullptr},
    {RsxrFeature::Diagrams, &g_rsxr_diagrams, nullptr},
    {RsxrFeature::Publish, &g_rsxr_publish_ports, &g_rsxr_native_publish},
    {RsxrFeature::Chords, &g_rsxr_native_chordloops, nullptr},
    {RsxrFeature::Tuner, &g_rsxr_native_tuner, nullptr},
    {RsxrFeature::Clite, &g_rsxr_clite, nullptr},
    {RsxrFeature::Glow, &g_rsxr_glow_fix, nullptr},
    {RsxrFeature::MenuLoad, &g_rsxr_menu_retry, nullptr},
};

// Marks one group failed and clears its switches (no cascade).
void MarkOff(RsxrFeature f) {
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

bool Rsxr_FeatureDisabled(RsxrFeature f) {
    return size_t(f) < size_t(RsxrFeature::Count) && g_featureOff[size_t(f)];
}

void Rsxr_ResetFeatureFailures() {
    for (auto& b : g_featureOff) {
        b = false;
    }
}

bool Rsxr_DisableFeature(RsxrFeature f) {
    if (f == RsxrFeature::Core || size_t(f) >= size_t(RsxrFeature::Count)) {
        return false;
    }

    MarkOff(f);

    // Native 7-string mode is both halves: C-lite (the guitar is built with the count) and the native_publish
    // ports (the layouts that can show it). One half failing takes the other with it: a half cannot stand alone.
    if (f == RsxrFeature::Clite && g_rsxr_publish_ports) {
        MarkOff(RsxrFeature::Publish);
    }

    if (f == RsxrFeature::Publish && g_rsxr_clite) {
        MarkOff(RsxrFeature::Clite);
    }

    // The native tuner session carries copies of the GuitarStrings / FretGrid / colour edits: without any of them it
    // must go too (its switch only; the Tuner group itself stays).
    if (!g_rsxr_native_strings || !g_rsxr_native_fretgrid || !g_rsxr_native_colours) {
        g_rsxr_native_tuner = 0;
    }

    // A guitar built with 7 needs the native PegSet: GKC writes peg request 7/8 and the stock
    // Rnd_PegSet_Load 0x59C4D0 would loop over it on the six-slot stock layout. Any loss of Pegs (startup config or a
    // runtime isolation after the initial configuration) takes clite and the ports with it before the next round.
    if (!g_rsxr_native_pegset && (g_rsxr_clite || g_rsxr_publish_ports)) {
        MarkOff(RsxrFeature::Clite);
        MarkOff(RsxrFeature::Publish);
    }

    // A guitar built with 7 needs the native 8-slot tuner session (docs/ARCHITECTURE.md): the stock
    // session's per-string arrays have six slots and its loops run to info+0Bh. clite goes, and native_publish with it.
    if (!g_rsxr_native_tuner && g_rsxr_clite) {
        MarkOff(RsxrFeature::Clite);

        if (g_rsxr_publish_ports) {
            MarkOff(RsxrFeature::Publish);
        }
    }

    // native_publish needs all five native layouts (rsxrpub::PortsEnabled); chordloops needs native_strings.
    if (g_rsxr_publish_ports && (!g_rsxr_native_strings || !g_rsxr_native_fretgrid || !g_rsxr_native_colours ||
                                 !g_rsxr_native_pitch || !g_rsxr_native_nextgrid)) {
        MarkOff(RsxrFeature::Publish);

        if (g_rsxr_clite) {  // ... and without the ports a guitar built with 7 strings has nothing to show it
            MarkOff(RsxrFeature::Clite);
        }
    }

    if (g_rsxr_native_chordloops && !g_rsxr_native_strings) {
        MarkOff(RsxrFeature::Chords);
    }

    return true;
}

// Parameters are switches the tests vary; other switches are owned by their features. Every
// write condition comes from registration metadata, so site renames do not alter patch selection.
bool Rsxr_ShouldPatch(const RsxrHook& h, bool diagnostics, bool privateOn, bool nativePegs, bool nativeStrings) {
    if (g_featureOff[size_t(Rsxr_FeatureOf(h))]) {
        return false;
    }
    if ((h.gates & kRsxrGateMenuLoad) && !g_rsxr_menu_retry) {
        return false;
    }
    if ((h.gates & kRsxrGateTuner) &&
        (!g_rsxr_native_tuner || !nativeStrings || !g_rsxr_native_fretgrid || !g_rsxr_native_colours)) {
        return false;
    }
    if ((h.gates & kRsxrGatePublish) && !g_rsxr_publish_ports) {
        return false;
    }
    if ((h.gates & kRsxrGateClite) && !g_rsxr_clite) {
        return false;
    }
    if ((h.gates & kRsxrGateNoClite) && g_rsxr_clite) {
        return false;
    }
    if ((h.gates & kRsxrGatePublishTrace) && !g_rsxr_publish_trace) {
        return false;
    }
    if ((h.gates & kRsxrGateNoPublish) && g_rsxr_publish_ports) {
        return false;
    }
    if ((h.gates & kRsxrGateChordloops) && (!g_rsxr_native_chordloops || !nativeStrings)) {
        return false;
    }
    if ((h.gates & kRsxrGateColours) && !g_rsxr_native_colours) {
        return false;
    }
    if ((h.gates & kRsxrGateTestVariant) && !RsxrVariant_Get().Any()) {
        return false;
    }
    if ((h.gates & kRsxrGateGlow) && !g_rsxr_glow_fix) {
        return false;
    }
    if ((h.gates & kRsxrGateDiagrams) && !g_rsxr_diagrams) {
        return false;
    }
    if ((h.gates & kRsxrGatePegs) && !nativePegs) {
        return false;
    }
    if ((h.gates & kRsxrGateStrings) && !nativeStrings) {
        return false;
    }
    if ((h.gates & kRsxrGateFretgrid) && !g_rsxr_native_fretgrid) {
        return false;
    }
    if ((h.gates & kRsxrGatePitch) && !g_rsxr_native_pitch) {
        return false;
    }
    if ((h.gates & kRsxrGateNextgrid) && !g_rsxr_native_nextgrid) {
        return false;
    }

    return !h.ref && (diagnostics || !h.diagOnly) && (privateOn || !h.privateOnly);
}
