#include "features/colours/colours.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "test.h"
#include <cstring>

TEST(colour_hooks_are_the_cm_sites) {
    const uintptr_t saved = g_rs7_native_colours;
    int cm = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref || std::strncmp(h->id, "CM", 2)) {
            continue;
        }

        ++cm;
        CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Colours);  // the feature's own table names the group
        g_rs7_native_colours = 0;
        CHECK(!Rs7_ShouldPatch(*h, true, true, true, true));
        g_rs7_native_colours = 1;
        CHECK(Rs7_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(size_t(cm), kRs7ColourHooks.count);
    CHECK(Rs7_IsSevenUnit(Rs7Feature::Colours));
    g_rs7_native_colours = saved;
}

TEST(glow_hook_follows_the_rsmods_glow_switch) {
    const uintptr_t saved = g_rs7_glow_fix;
    const Rs7Hook* h = Rs7_FindHook("GLW");

    CHECK(h && h->callback && !h->stub);
    if (!h) {
        return;
    }

    CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Glow);
    CHECK(!Rs7_IsSevenUnit(Rs7Feature::Glow));
    g_rs7_glow_fix = 0;
    CHECK(!Rs7_ShouldPatch(*h, true));
    g_rs7_glow_fix = 1;
    CHECK(Rs7_ShouldPatch(*h, false));
    CHECK(Rs7_DisableFeature(Rs7Feature::Glow));
    CHECK_EQ(g_rs7_glow_fix, uintptr_t(0));
    Rs7_ResetFeatureFailures();
    g_rs7_glow_fix = saved;
}
