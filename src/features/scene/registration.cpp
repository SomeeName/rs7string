// The scene-graph reads' data references, looked up by src/registry.cpp (Rs7_FindHook); never written. Group Core,
// as every data reference.
#include "features/scene/scene.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Ref("X_WORLD", &g_rs7_fn_world),     Rs7_Ref("X_ROOT", &g_rs7_fn_root),
    Rs7_Ref("X_FADEMGR", &g_rs7_fade_mgr),   Rs7_Ref("X_FADEIN", &g_rs7_fn_fadein),
    Rs7_Ref("X_FADEOUT", &g_rs7_fn_fadeout),
};
}  // namespace

extern const Rs7HookTable kRs7SceneHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
