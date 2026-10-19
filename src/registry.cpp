// The hook table list (kTables) and the group lookup over it (hook_rules.h), the groups as isolate's policy.
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/diagrams/diagrams.h"
#include "features/fretgrid/fretgrid.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/psarc/psarc.h"
#include "features/publish/publish.h"
#include "features/scene/scene.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "features/testdrive/testdrive.h"
#include "features/tuner/tuner.h"
#include "features/vguit/vguit.h"
#include "hook_rules.h"
#include "platform/isolate.h"
#include <cstring>

namespace {
// One line per feature table (src/features/<name>/registration.cpp).
const Rs7HookTable* const kTables[] = {
    &kRs7ColourRefHooks,  &kRs7DiagHooks,         &kRs7SongEndHooks,    &kRs7LoadProbeHooks, &kRs7CliteHooks,
    &kRs7PsarcHooks,      &kRs7VguitHooks,        &kRs7TunerHooks,      &kRs7TunerCoreHooks, &kRs7ColourHooks,
    &kRs7GlowHooks,       &kRs7ChordHooks,        &kRs7DiagramHooks,    &kRs7HeadstockHooks, &kRs7HeadstockCoreHooks,
    &kRs7PublishHooks,    &kRs7FretgridHooks,     &kRs7NextgridHooks,   &kRs7FretHooks,      &kRs7StringsHooks,
    &kRs7SceneHooks,      &kRs7String7Hooks,      &kRs7PitchHooks,      &kRs7PitchCoreHooks, &kRs7NslotHooks,
    &kRs7ChordFrameHooks, &kRs7FretgridCoreHooks, &kRs7TestVariantHooks};
constexpr size_t kTableCount = sizeof kTables / sizeof kTables[0];

// kRs7Groups: the platform's integer groups to and from Rs7Feature.
Rs7Group OfSite(const char* id) {
    const Rs7Hook* h = Rs7_FindHook(id);

    return Rs7_Group(h ? Rs7_FeatureOf(*h) : Rs7Feature::Core);
}

bool IsUnit(Rs7Group g) {
    return Rs7_IsSevenUnit(Rs7Feature(g));
}

bool Disable(Rs7Group g) {
    return Rs7_DisableFeature(Rs7Feature(g));
}

bool Disabled(Rs7Group g) {
    return Rs7_FeatureDisabled(Rs7Feature(g));
}

const char* Name(Rs7Group g) {
    return Rs7_FeatureName(Rs7Feature(g));
}
}  // namespace

const Rs7Hook* Rs7_FindHook(const char* id) {
    return Rs7_FindHookIn(kTables, kTableCount, id);
}

Rs7Feature Rs7_FeatureOf(const Rs7Hook& h) {
    if (h.ref) {
        return Rs7Feature::Core;  // data references are shared with the core
    }

    const Rs7HookTable* t = Rs7_TableOf(kTables, kTableCount, h);

    return t ? Rs7Feature(t->group) : Rs7Feature::Core;  // a feature's own table names its group
}

const Rs7GroupPolicy kRs7Groups = {size_t(Rs7Feature::Count), &OfSite, &IsUnit, &Disable, &Disabled, &Name};
const Rs7PlainHost kRs7PlainHost = {&Rs7_FindHook, &kRs7Groups};
