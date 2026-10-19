// The diagnostics' hooks (group Diag), looked up by src/registry.cpp (Rs7_FindHook): D2 D3 D5 are written only with
// [debug] diagnostics=1 (Rs7_ShouldPatch, the rows' diagOnly).
#include "features/diag/diag.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_D2(Rs7Mid::Context&);  // hooks.cpp
void Rs7_Mid_D3(Rs7Mid::Context&);
void Rs7_Mid_D5(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    {"D2", nullptr, nullptr, nullptr, true, false, false, 0, 0, &Rs7_Mid_D2},
    {"D3", nullptr, nullptr, nullptr, true, false, false, 0, 0, &Rs7_Mid_D3},
    {"D5", nullptr, nullptr, nullptr, true, false, false, 0, 0, &Rs7_Mid_D5},
};
}  // namespace

extern const Rs7HookTable kRs7DiagHooks = {Rs7_Group(Rs7Feature::Diag), kHooks, sizeof kHooks / sizeof kHooks[0]};
