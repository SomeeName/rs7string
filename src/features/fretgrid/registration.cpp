// The native FretGrid's hooks (group Fretgrid): looked up by src/registry.cpp (Rs7_FindHook). Every row carries
// kRs7FgNative: written only with the native FretGrid (Rs7_ShouldPatch). The FGR<va> rows are in-place byte patches
// (null stub; the bytes come from sites.json) that startup defers to the first FGHK call on the game thread; FGCT /
// FGHK / FGRB are mid-hooks (hooks.cpp, hooks_build.cpp; FGRB is not written with the native publication ports).
#include "features/fretgrid/fretgrid.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_V2(Rs7Mid::Context&);  // hooks_slide.cpp
extern uintptr_t g_rs7_resume_V2;
}

namespace {
const Rs7Hook kHooks[] = {
#define RS7_FGR(id) {#id, nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, kRs7FgNative}
#include "features/fretgrid/sites.generated.inc"
#undef RS7_FGR
    {"FGCT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_FGCT, kRs7FgNative},
    {"FGHK", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_FGHK, kRs7FgNative},
    {"FGRB", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_FGRB, kRs7FgNative},
};

// V2, the slide end cell stride (group Core, as before the move: written with the stock and the native FretGrid).
const Rs7Hook kCoreHooks[] = {
    {"V2", nullptr, &g_rs7_resume_V2, nullptr, false, false, false, 0, 0, &Rs7_Mid_V2, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
};
}  // namespace

extern const Rs7HookTable kRs7FretgridHooks = {Rs7_Group(Rs7Feature::Fretgrid), kHooks,
                                               sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7FretgridCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                   sizeof kCoreHooks / sizeof kCoreHooks[0]};
