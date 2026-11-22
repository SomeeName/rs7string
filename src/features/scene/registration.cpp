// The scene-graph reads' data references, looked up by src/registry.cpp (Rsxr_FindHook); never written. Group Core,
// as every data reference.
#include "features/scene/scene.h"
#include "model/groups.h"

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Ref("X_WORLD", &g_rsxr_fn_world),     Rsxr_Ref("X_ROOT", &g_rsxr_fn_root),
    Rsxr_Ref("X_FADEMGR", &g_rsxr_fade_mgr),   Rsxr_Ref("X_FADEIN", &g_rsxr_fn_fadein),
    Rsxr_Ref("X_FADEOUT", &g_rsxr_fn_fadeout),
};
}  // namespace

extern const RsxrHookTable kRsxrSceneHooks = {Rsxr_Group(RsxrFeature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
