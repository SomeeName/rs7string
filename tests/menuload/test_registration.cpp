#include "features/menuload/menuload.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "model/groups.h"
#include "test.h"
#include "testutil.h"

TEST(menu_retry_is_optional_and_failure_isolated) {
    rs7test::Restore<uintptr_t> restore(g_rs7_menu_retry);

    Rs7_ResetFeatureFailures();

    for (const auto& id : {"MenuLoadUpdate", "MenuMovieSetup", "MenuMovieResult", "MenuIndexPublish"}) {
        const auto* h = Rs7_FindHook(id);

        CHECK(h);
        CHECK(Rs7_FeatureOf(*h) == Rs7Feature::MenuLoad);
        CHECK(h->gates & kRs7GateMenuLoad);
        g_rs7_menu_retry = 1;
        CHECK(Rs7_ShouldPatch(*h, false, false, true, true));
        g_rs7_menu_retry = 0;
        CHECK(!Rs7_ShouldPatch(*h, false, false, true, true));
    }

    // The request call is a read-only reference, never patched; gated to this feature, so another mod's detour there
    // isolates only the menu fix, not the core.
    const auto* request = Rs7_FindHook("MenuMovieRequest");

    CHECK(request);
    CHECK(Rs7_FeatureOf(*request) == Rs7Feature::MenuLoad);

    g_rs7_menu_retry = 1;
    CHECK(!Rs7_IsSevenUnit(Rs7Feature::MenuLoad));
    CHECK(Rs7_DisableFeature(Rs7Feature::MenuLoad));
    CHECK_EQ(g_rs7_menu_retry, uintptr_t(0));
    CHECK(!Rs7_FeatureDisabled(Rs7Feature::Core));
    Rs7_ResetFeatureFailures();
}
