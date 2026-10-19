// The colour feature's hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch, the CM*
// sites only with g_rs7_native_colours, GLW only with g_rs7_glow_fix (Rs7_ShouldPatch).
#include "features/colours/colours.h"
#include "model/groups.h"

namespace {
const Rs7Hook kColourHooks[] = {
#include "features/colours/colour_hooks.generated.h"
};
const Rs7Hook kGlowHooks[] = {
    // GLW: RSMods' custom colours give a pure red string a near-black glow (Rs7_GlowFixPtr).
    {"GLW", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GLW},
};
// The colour manager and the NiTexture list (the ramp swap, d3d/worker.cpp; the headstock textures): data
// references, group Core.
const Rs7Hook kRefHooks[] = {
    {"X_CM", nullptr, nullptr, &g_rs7_colormgr, false, true},
    {"X_TCS", nullptr, nullptr, &g_rs7_texlist_cs, false, true},
    {"X_THEAD", nullptr, nullptr, &g_rs7_texlist_head, false, true},
};
}  // namespace

extern const Rs7HookTable kRs7ColourHooks = {Rs7_Group(Rs7Feature::Colours), kColourHooks,
                                             sizeof kColourHooks / sizeof kColourHooks[0]};
extern const Rs7HookTable kRs7GlowHooks = {Rs7_Group(Rs7Feature::Glow), kGlowHooks,
                                           sizeof kGlowHooks / sizeof kGlowHooks[0]};
extern const Rs7HookTable kRs7ColourRefHooks = {Rs7_Group(Rs7Feature::Core), kRefHooks,
                                                sizeof kRefHooks / sizeof kRefHooks[0]};
