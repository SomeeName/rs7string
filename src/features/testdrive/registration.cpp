// The test variants' hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch only when a
// [test] force_* key is on (Rs7_ShouldPatch, ids TV*).
#include "features/testdrive/testdrive.h"
#include "model/groups.h"
#include "platform/hooktable.h"

extern "C" {
void Rs7_Mid_TVCB(Rs7Mid::Context&);  // hooks_variants.cpp
void Rs7_Mid_TVGP(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    {"TVCB", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TVCB},
    {"TVGP", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TVGP},
};
}  // namespace

extern const Rs7HookTable kRs7TestVariantHooks = {Rs7_Group(Rs7Feature::Diag), kHooks,
                                                  sizeof kHooks / sizeof kHooks[0]};
