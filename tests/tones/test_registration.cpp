#include "features/tones/tones.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "model/groups.h"
#include "test.h"
#include "testutil.h"

TEST(tone_hooks_form_one_optional_failure_group) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);

    Rsxr_ResetFeatureFailures();

    for (const auto* id :
         {"ToneLoad", "ToneAuto", "ToneSetup", "ToneSeek", "ToneResume", "ToneOverride", "ToneRefresh", "ToneApply",
          "ToneManager", "ToneRegister", "ToneRegisterCall", "ToneResourceDestroy", "TonePlanBuild", "TonePlanField",
          "TonePlanExtension", "TonePlanBound", "TonePlanEdit", "ToneContext"}) {
        const auto* h = Rsxr_FindHook(id);

        CHECK(h);
        CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Tones);
        CHECK(h->gates & kRsxrGateTones);
        g_rsxr_tones = 1;
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true) == !h->ref);
        g_rsxr_tones = 0;
        CHECK(!Rsxr_ShouldPatch(*h, false, false, true, true));
    }

    g_rsxr_tones = 1;
    CHECK(!Rsxr_IsSevenUnit(RsxrFeature::Tones));
    CHECK(Rsxr_DisableFeature(RsxrFeature::Tones));
    CHECK_EQ(g_rsxr_tones, uintptr_t(0));
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Core));
    Rsxr_ResetFeatureFailures();
}
