#pragma once
#include "platform/hooktable.h"

// Feature groups for failure isolation. A site that cannot be resolved / prepared / written disables only
// its own group (Rs7_DisableFeature); Core (the base 7-string sites, X_ refs and everything unlisted)
// cannot be isolated: a Core failure still aborts patching.
enum class Rs7Feature {
    Core,
    Colours,
    Pegs,
    Strings,
    Fretgrid,
    Pitch,
    Nextgrid,
    Diagrams,
    Vguitar,
    Diag,
    Publish,
    Chords,
    Tuner,
    Psarc,
    Clite,
    Glow,
    Count
};

// The platform's group value of a feature (Rs7HookTable::group, platform/isolate.h).
constexpr Rs7Group Rs7_Group(Rs7Feature f) {
    return Rs7Group(f);
}

const char* Rs7_FeatureName(Rs7Feature f);
// The native 7-string unit (Colours, Pegs, Strings, Fretgrid, Pitch, Nextgrid, Publish, Tuner, Clite): since 1.0 there
// is no legacy fallback for any of them, so at startup a member that fails (resolve, preparation, writing, or a
// startup check) is handled like a core failure: nothing is patched and the game stays stock (six strings). A loss at
// runtime (Rs7_DisableFeature from a deferred FretGrid write) still only clears the switches the stubs read.
bool Rs7_IsSevenUnit(Rs7Feature f);
