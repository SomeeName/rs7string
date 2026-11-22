// The native chart hooks (native.h), looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch.
#include "features/native/native.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_XI1(RsxrMid::Context&);  // hooks.cpp
void Rsxr_Mid_JM1(RsxrMid::Context&);
void Rsxr_Mid_PN1(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_XI1;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("PN1", &Rsxr_Mid_PN1),
    Rsxr_Mid("JM1", &Rsxr_Mid_JM1),
    Rsxr_Mid("XI1", &Rsxr_Mid_XI1, &g_rsxr_resume_XI1),
};
}  // namespace

extern const RsxrHookTable kRsxrNativeHooks = {Rsxr_Group(RsxrFeature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
