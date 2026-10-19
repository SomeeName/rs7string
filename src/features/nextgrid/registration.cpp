// The native next-note grid's hooks (group Nextgrid): looked up by src/registry.cpp (Rs7_FindHook) and written by
// the main batch. Every site row carries kRs7NgNative: written only with the native next-note grid (Rs7_ShouldPatch).
// The NGB<va> rows are in-place byte patches (null stub; the bytes come from sites.json), NG1 / NG9 are mid-hooks
// (hooks_mid.cpp, NG1 light), NGP a stub (hooks_stubs.cpp). The X_SPAWN / X_ADV3 refs (the game functions NG9's row-6
// spawn calls, hooks.cpp) are data references and stay in the Core group (Rs7_FeatureOf).
#include "features/nextgrid/nextgrid.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_NG1(Rs7Mid::Context&);
void Rs7_Stub_NGP();
void Rs7_Mid_NG9(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_NG1, g_rs7_resume_NGP, g_rs7_resume_NG9, g_rs7_target_NG9;
}

namespace {
// Native next-note grid site: written only with the native next-note grid.
Rs7Hook NgHook(const char* id, void* stub = nullptr, uintptr_t* resume = nullptr, uintptr_t* target = nullptr,
               Rs7Mid::Callback callback = nullptr, Rs7Mid::Mode mode = Rs7Mid::Mode::Full) {
    Rs7Hook h{id, stub, resume, target, false, false};

    h.ng = kRs7NgNative;
    h.callback = callback;
    h.midMode = mode;

    return h;
}

const Rs7Hook kHooks[] = {
    {"X_SPAWN", nullptr, nullptr, &g_rs7_fn_spawn, false, true},
    {"X_ADV3", nullptr, nullptr, &g_rs7_fn_adv3, false, true},
#define RS7_NGB(id) NgHook(#id)
#include "features/nextgrid/sites.generated.inc"
#undef RS7_NGB
    NgHook("NG1", nullptr, &g_rs7_resume_NG1, nullptr, &Rs7_Mid_NG1, Rs7Mid::Mode::Light),
    NgHook("NGP", reinterpret_cast<void*>(&Rs7_Stub_NGP), &g_rs7_resume_NGP),
    NgHook("NG9", nullptr, &g_rs7_resume_NG9, &g_rs7_target_NG9, &Rs7_Mid_NG9),
};
}  // namespace

extern const Rs7HookTable kRs7NextgridHooks = {Rs7_Group(Rs7Feature::Nextgrid), kHooks,
                                               sizeof kHooks / sizeof kHooks[0]};
