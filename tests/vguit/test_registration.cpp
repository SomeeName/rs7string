#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "test.h"
#include <cstring>

TEST(vguitar_only_hooks_are_the_four_vg_sites) {
    int vg = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool want = !std::strcmp(h->id, "TICK") || !std::strcmp(h->id, "JUD1") || !std::strcmp(h->id, "JUD2") ||
                          !std::strcmp(h->id, "AUD1");

        CHECK_EQ(h->vgOnly, want);
        if (!want) {
            continue;
        }

        ++vg;
        CHECK(!h->diagOnly);
        CHECK(Rs7_FeatureOf(*h) == Rs7Feature::Vguitar);                   // the feature's own table names the group
        CHECK(!Rs7_ShouldPatch(*h, false) && !Rs7_ShouldPatch(*h, true));  // [test] vguitar=0
        CHECK(Rs7_ShouldPatch(*h, false, true) && Rs7_ShouldPatch(*h, true, true));  // vguitar=1
    }

    CHECK_EQ(vg, 4);
}
