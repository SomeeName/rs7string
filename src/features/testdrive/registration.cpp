// The test variants' hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch only when a
// [test] force_* key is on (Rs7_ShouldPatch, ids TV*).
#include "features/testdrive/testdrive.h"
#include "model/gates.h"
#include "model/groups.h"
#include "platform/hooktable.h"

extern "C" {
void Rs7_Mid_TVCB(Rs7Mid::Context&);  // hooks_variants.cpp
void Rs7_Mid_TVGP(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("TVCB", &Rs7_Mid_TVCB).Gate(kRs7GateTestVariant),
    Rs7_Mid("TVGP", &Rs7_Mid_TVGP).Gate(kRs7GateTestVariant),
};
}  // namespace

extern const Rs7HookTable kRs7TestVariantHooks = {Rs7_Group(Rs7Feature::Diag), kHooks,
                                                  sizeof kHooks / sizeof kHooks[0]};
