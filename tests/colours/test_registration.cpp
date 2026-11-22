#include "features/colours/colours.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

TEST(colour_hooks_are_the_cm_ports_and_the_cb_getters) {
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);
    int cm = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || h->ref || (std::strncmp(h->id, "CM", 2) && std::strncmp(h->id, "CB", 2))) {
            continue;
        }

        ++cm;
        CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Colours);  // the feature's own table names the group
        g_rsxr_native_colours = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true, true, true, true));
        g_rsxr_native_colours = 1;
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(size_t(cm), kRsxrColourHooks.count);
    CHECK(Rsxr_IsSevenUnit(RsxrFeature::Colours));
}

TEST(glow_hook_follows_the_rsmods_glow_switch) {
    rsxrtest::Restore restoreGlow(g_rsxr_glow_fix);
    const RsxrHook* h = Rsxr_FindHook("GLW");

    CHECK(h && h->callback && !h->stub);
    if (!h) {
        return;
    }

    CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Glow);
    CHECK(!Rsxr_IsSevenUnit(RsxrFeature::Glow));
    g_rsxr_glow_fix = 0;
    CHECK(!Rsxr_ShouldPatch(*h, true));
    g_rsxr_glow_fix = 1;
    CHECK(Rsxr_ShouldPatch(*h, false));
    CHECK(Rsxr_DisableFeature(RsxrFeature::Glow));
    CHECK_EQ(g_rsxr_glow_fix, uintptr_t(0));
    Rsxr_ResetFeatureFailures();
}
