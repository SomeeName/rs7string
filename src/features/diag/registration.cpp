// The diagnostics' hooks (group Diag), looked up by src/registry.cpp (Rsxr_FindHook): D2 D3 D5 are written only with
// [debug] diagnostics=1 (Rsxr_ShouldPatch, the rows' diagOnly).
#include "features/diag/diag.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_D2(RsxrMid::Context&);  // hooks.cpp
void Rsxr_Mid_D3(RsxrMid::Context&);
void Rsxr_Mid_D5(RsxrMid::Context&);
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("D2", &Rsxr_Mid_D2).Diag(),
    Rsxr_Mid("D3", &Rsxr_Mid_D3).Diag(),
    Rsxr_Mid("D5", &Rsxr_Mid_D5).Diag(),
};
}  // namespace

extern const RsxrHookTable kRsxrDiagHooks = {Rsxr_Group(RsxrFeature::Diag), kHooks, sizeof kHooks / sizeof kHooks[0]};
