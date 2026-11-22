// The native FretGrid's hooks (group Fretgrid): looked up by src/registry.cpp (Rsxr_FindHook). Every row carries
// kRsxrFgNative: written only with the native FretGrid (Rsxr_ShouldPatch). The FGR<va> rows are in-place byte patches
// (null stub; the bytes come from sites.json) that startup defers to the first FGHK call on the game thread; FGCT /
// FGHK / FGRB are mid-hooks (hooks.cpp, hooks_build.cpp; FGRB is not written with the native publication ports).
#include "features/fretgrid/fretgrid.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_V2(RsxrMid::Context&);  // hooks_slide.cpp
extern uintptr_t g_rsxr_resume_V2;
}

namespace {
const RsxrHook kHooks[] = {
#define RSXR_FGR(id) Rsxr_Site(#id).Gate(kRsxrGateFretgrid)
#include "features/fretgrid/sites.generated.inc"
#undef RSXR_FGR
    Rsxr_Mid("FGCT", &Rsxr_Mid_FGCT).Gate(kRsxrGateFretgrid),
    Rsxr_Mid("FGHK", &Rsxr_Mid_FGHK).Gate(kRsxrGateFretgrid),
    Rsxr_Mid("FGRB", &Rsxr_Mid_FGRB).Gate(kRsxrGateNoPublish).Gate(kRsxrGateFretgrid),
};

// V2, the slide end cell stride (group Core, as before the move: written with the stock and the native FretGrid).
const RsxrHook kCoreHooks[] = {
    Rsxr_Mid("V2", &Rsxr_Mid_V2, &g_rsxr_resume_V2).Light(),
};
}  // namespace

extern const RsxrHookTable kRsxrFretgridHooks = {Rsxr_Group(RsxrFeature::Fretgrid), kHooks,
                                                 sizeof kHooks / sizeof kHooks[0]};
extern const RsxrHookTable kRsxrFretgridCoreHooks = {Rsxr_Group(RsxrFeature::Core), kCoreHooks,
                                                     sizeof kCoreHooks / sizeof kCoreHooks[0]};
