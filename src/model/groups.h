#pragma once
#include "platform/hooktable.h"

// Feature groups for failure isolation. A site that cannot be resolved / prepared / written disables only
// its own group (Rsxr_DisableFeature); Core (the base 7-string sites, X_ refs and everything unlisted)
// cannot be isolated: a Core failure still aborts patching.
enum class RsxrFeature {
    Core,
    Colours,
    Pegs,
    Strings,
    Fretgrid,
    Pitch,
    Nextgrid,
    Diagrams,
    Private,  // the private extras (src/features/extras/extras.h)
    Diag,
    Publish,
    Chords,
    Tuner,
    Psarc,
    Clite,
    Glow,
    MenuLoad,
    Count
};

// The platform's group value of a feature (RsxrHookTable::group, platform/isolate.h).
constexpr RsxrGroup Rsxr_Group(RsxrFeature f) {
    return RsxrGroup(f);
}

const char* Rsxr_FeatureName(RsxrFeature f);
// The native 7-string unit (Colours, Pegs, Strings, Fretgrid, Pitch, Nextgrid, Publish, Tuner, Clite): since 1.0 there
// is no legacy fallback for any of them, so at startup a member that fails (resolve, preparation, writing, or a
// startup check) is handled like a core failure: nothing is patched and the game stays stock (six strings). A loss at
// runtime (Rsxr_DisableFeature from a deferred FretGrid write) still only clears the switches the stubs read.
bool Rsxr_IsSevenUnit(RsxrFeature f);
