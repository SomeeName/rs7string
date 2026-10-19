// The scene-graph reads' data references, looked up by src/registry.cpp (Rs7_FindHook); never written. Group Core,
// as every data reference.
#include "features/scene/scene.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    {"X_WORLD", nullptr, nullptr, &g_rs7_fn_world, false, true},
    {"X_ROOT", nullptr, nullptr, &g_rs7_fn_root, false, true},
    {"X_FADEMGR", nullptr, nullptr, &g_rs7_fade_mgr, false, true},
    {"X_FADEIN", nullptr, nullptr, &g_rs7_fn_fadein, false, true},
    {"X_FADEOUT", nullptr, nullptr, &g_rs7_fn_fadeout, false, true},
};
}  // namespace

extern const Rs7HookTable kRs7SceneHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
