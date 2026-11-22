#include "features/chords/chords.h"
#include "features/strings/strings.h"
#include "hook_rules.h"
#include "test.h"
#include "testutil.h"

TEST(chordloops_require_native_strings) {
    const RsxrHook* h = Rsxr_FindHook("CLGU");

    CHECK(h != nullptr);
    if (!h) {
        return;
    }

    rsxrtest::Restore restore_g_rsxr_native_chordloops(g_rsxr_native_chordloops);

    g_rsxr_native_chordloops = 0;
    CHECK(!Rsxr_ShouldPatch(*h, false, false, false, true));
    g_rsxr_native_chordloops = 1;
    CHECK(!Rsxr_ShouldPatch(*h, false, false, false, false));
    CHECK(Rsxr_ShouldPatch(*h, false, false, false, true));

    for (const char* id : {"N22", "N23", "NF2", "NF3", "CT4", "CT5", "CT6", "CT7", "CT8", "CT9", "CT10", "CT11"}) {
        CHECK(Rsxr_FindHook(id) == nullptr);  // the seven-slot chord-loop sites went with [chords] native_chordloops
    }
}
