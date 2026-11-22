// The native next-note grid's hooks (group Nextgrid): looked up by src/registry.cpp (Rsxr_FindHook) and written by
// the main batch. Every site row carries kRsxrNgNative: written only with the native next-note grid (Rsxr_ShouldPatch).
// The NGB<va> rows are in-place byte patches (null stub; the bytes come from sites.json), NG1 / NG9 are mid-hooks
// (hooks_mid.cpp, NG1 light), NGP a stub (hooks_stubs.cpp). The X_SPAWN / X_ADV3 refs (the game functions NG9's row-6
// spawn calls, hooks.cpp) are data references and stay in the Core group (Rsxr_FeatureOf).
#include "features/nextgrid/nextgrid.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_NG1(RsxrMid::Context&);
void Rsxr_Stub_NGP();
void Rsxr_Mid_NG9(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_NG1, g_rsxr_resume_NGP, g_rsxr_resume_NG9, g_rsxr_target_NG9;
}

namespace {
// Native next-note grid site: written only with the native next-note grid.
constexpr RsxrHook NgHook(const char* id, void* stub = nullptr, uintptr_t* resume = nullptr,
                          uintptr_t* target = nullptr, RsxrMid::Callback callback = nullptr,
                          RsxrMid::Mode mode = RsxrMid::Mode::Full) {
    RsxrHook h = Rsxr_Stub(id, stub, resume, target).Gate(kRsxrGateNextgrid).InMode(mode);

    h.callback = callback;

    return h;
}

const RsxrHook kHooks[] = {
    Rsxr_Ref("X_SPAWN", &g_rsxr_fn_spawn),
    Rsxr_Ref("X_ADV3", &g_rsxr_fn_adv3),
#define RSXR_NGB(id) NgHook(#id)
#include "features/nextgrid/sites.generated.inc"
#undef RSXR_NGB
    NgHook("NG1", nullptr, &g_rsxr_resume_NG1, nullptr, &Rsxr_Mid_NG1, RsxrMid::Mode::Light),
    NgHook("NGP", reinterpret_cast<void*>(&Rsxr_Stub_NGP), &g_rsxr_resume_NGP),
    NgHook("NG9", nullptr, &g_rsxr_resume_NG9, &g_rsxr_target_NG9, &Rsxr_Mid_NG9),
};
}  // namespace

extern const RsxrHookTable kRsxrNextgridHooks = {Rsxr_Group(RsxrFeature::Nextgrid), kHooks,
                                                 sizeof kHooks / sizeof kHooks[0]};
