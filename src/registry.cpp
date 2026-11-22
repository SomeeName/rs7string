// The hook table list (kTables) and the group lookup over it (hook_rules.h), the groups as isolate's policy.
#include "features/arrangements/arrangements.h"
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/diagrams/diagrams.h"
#include "features/extras/extras.h"
#include "features/fretgrid/fretgrid.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/menuload/menuload.h"
#include "features/native/native.h"
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
#include "hook_rules.h"
#include "platform/isolate.h"
#include <cstring>

namespace {
// One line per feature table (src/features/<name>/registration.cpp).
const RsxrHookTable* const kTables[] = {
    &kRsxrColourRefHooks,  &kRsxrDiagHooks,          &kRsxrSongEndHooks,     &kRsxrLoadProbeHooks,
    &kRsxrCliteHooks,      &kRsxrPsarcHooks,         &kRsxrTunerHooks,       &kRsxrTunerCoreHooks,
    &kRsxrColourHooks,     &kRsxrGlowHooks,          &kRsxrChordHooks,       &kRsxrDiagramHooks,
    &kRsxrHeadstockHooks,  &kRsxrHeadstockCoreHooks, &kRsxrPublishHooks,     &kRsxrFretgridHooks,
    &kRsxrNextgridHooks,   &kRsxrFretHooks,          &kRsxrStringsHooks,     &kRsxrSceneHooks,
    &kRsxrString7Hooks,    &kRsxrPitchHooks,         &kRsxrPitchCoreHooks,   &kRsxrNslotHooks,
    &kRsxrChordFrameHooks, &kRsxrFretgridCoreHooks,  &kRsxrTestVariantHooks, &kRsxrArrangementHooks,
    &kRsxrNslotEightHooks, &kRsxrMenuLoadHooks,      &kRsxrNativeHooks,
#if RSXR_PRIVATE
    &kRsxrPrivateHooks,  // the private extras (src/features/extras/extras.h)
#endif
};
constexpr size_t kTableCount = sizeof kTables / sizeof kTables[0];

// kRsxrGroups: the platform's integer groups to and from RsxrFeature.
RsxrGroup OfSite(const char* id) {
    const RsxrHook* h = Rsxr_FindHook(id);

    return Rsxr_Group(h ? Rsxr_FeatureOf(*h) : RsxrFeature::Core);
}

bool IsUnit(RsxrGroup g) {
    return Rsxr_IsSevenUnit(RsxrFeature(g));
}

bool Disable(RsxrGroup g) {
    return Rsxr_DisableFeature(RsxrFeature(g));
}

bool Disabled(RsxrGroup g) {
    return Rsxr_FeatureDisabled(RsxrFeature(g));
}

const char* Name(RsxrGroup g) {
    return Rsxr_FeatureName(RsxrFeature(g));
}
}  // namespace

const RsxrHook* Rsxr_FindHook(const char* id) {
    return Rsxr_FindHookIn(kTables, kTableCount, id);
}

RsxrFeature Rsxr_FeatureOf(const RsxrHook& h) {
    if (h.ref && !h.gates) {
        return RsxrFeature::Core;  // data references are shared with the core; a gated one serves only its feature
    }

    const RsxrHookTable* t = Rsxr_TableOf(kTables, kTableCount, h);

    return t ? RsxrFeature(t->group) : RsxrFeature::Core;  // a feature's own table names its group
}

const RsxrGroupPolicy kRsxrGroups = {size_t(RsxrFeature::Count), &OfSite, &IsUnit, &Disable, &Disabled, &Name};
const RsxrPlainHost kRsxrPlainHost = {&Rsxr_FindHook, &kRsxrGroups};
