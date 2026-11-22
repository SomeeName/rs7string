#include "features/menuload/menuload.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "model/groups.h"
#include "test.h"
#include "testutil.h"

TEST(menu_retry_is_optional_and_failure_isolated) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_menu_retry);

    Rsxr_ResetFeatureFailures();

    for (const auto& id : {"MenuLoadUpdate", "MenuMovieSetup", "MenuMovieResult", "MenuIndexPublish"}) {
        const auto* h = Rsxr_FindHook(id);

        CHECK(h);
        CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::MenuLoad);
        CHECK(h->gates & kRsxrGateMenuLoad);
        g_rsxr_menu_retry = 1;
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true));
        g_rsxr_menu_retry = 0;
        CHECK(!Rsxr_ShouldPatch(*h, false, false, true, true));
    }

    // The request call is a read-only reference, never patched; gated to this feature, so another mod's detour there
    // isolates only the menu fix, not the core.
    const auto* request = Rsxr_FindHook("MenuMovieRequest");

    CHECK(request);
    CHECK(Rsxr_FeatureOf(*request) == RsxrFeature::MenuLoad);

    g_rsxr_menu_retry = 1;
    CHECK(!Rsxr_IsSevenUnit(RsxrFeature::MenuLoad));
    CHECK(Rsxr_DisableFeature(RsxrFeature::MenuLoad));
    CHECK_EQ(g_rsxr_menu_retry, uintptr_t(0));
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Core));
    Rsxr_ResetFeatureFailures();
}
