// The native FretGrid's hooks (group Fretgrid): looked up by src/registry.cpp (Rs7_FindHook). Every row carries
// kRs7FgNative: written only with the native FretGrid (Rs7_ShouldPatch). The FGR<va> rows are in-place byte patches
// (null stub; the bytes come from sites.json) that startup defers to the first FGHK call on the game thread; FGCT /
// FGHK / FGRB are mid-hooks (hooks.cpp, hooks_build.cpp; FGRB is not written with the native publication ports).
#include "features/fretgrid/fretgrid.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_V2(Rs7Mid::Context&);  // hooks_slide.cpp
extern uintptr_t g_rs7_resume_V2;
}

namespace {
const Rs7Hook kHooks[] = {
#define RS7_FGR(id) Rs7_Site(#id).Gate(kRs7GateFretgrid)
#include "features/fretgrid/sites.generated.inc"
#undef RS7_FGR
    Rs7_Mid("FGCT", &Rs7_Mid_FGCT).Gate(kRs7GateFretgrid),
    Rs7_Mid("FGHK", &Rs7_Mid_FGHK).Gate(kRs7GateFretgrid),
    Rs7_Mid("FGRB", &Rs7_Mid_FGRB).Gate(kRs7GateNoPublish).Gate(kRs7GateFretgrid),
};

// V2, the slide end cell stride (group Core, as before the move: written with the stock and the native FretGrid).
const Rs7Hook kCoreHooks[] = {
    Rs7_Mid("V2", &Rs7_Mid_V2, &g_rs7_resume_V2).Light(),
};
}  // namespace

extern const Rs7HookTable kRs7FretgridHooks = {Rs7_Group(Rs7Feature::Fretgrid), kHooks,
                                               sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7FretgridCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                   sizeof kCoreHooks / sizeof kCoreHooks[0]};
