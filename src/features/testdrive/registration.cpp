// The test variants' hooks: looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch only when a
// [test] force_* key is on (Rsxr_ShouldPatch, ids TV*).
#include "features/testdrive/testdrive.h"
#include "model/gates.h"
#include "model/groups.h"
#include "platform/hooktable.h"

extern "C" {
void Rsxr_Mid_TVCB(RsxrMid::Context&);  // hooks_variants.cpp
void Rsxr_Mid_TVGP(RsxrMid::Context&);
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("TVCB", &Rsxr_Mid_TVCB).Gate(kRsxrGateTestVariant),
    Rsxr_Mid("TVGP", &Rsxr_Mid_TVGP).Gate(kRsxrGateTestVariant),
};
}  // namespace

extern const RsxrHookTable kRsxrTestVariantHooks = {Rsxr_Group(RsxrFeature::Diag), kHooks,
                                                    sizeof kHooks / sizeof kHooks[0]};
