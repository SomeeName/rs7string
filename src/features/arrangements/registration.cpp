// The arrangement page hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch. Group Core:
// nothing changes for songs with five (co-op: eight) or fewer arrangements.
#include "features/arrangements/arrangements.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_ALTS(RsxrMid::Context&);  // hooks.cpp
void Rsxr_Mid_ALTF(RsxrMid::Context&);
void Rsxr_Mid_ALTL(RsxrMid::Context&);
void Rsxr_Mid_ALTR(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_ALTS, g_rsxr_target_ALTF, g_rsxr_target_ALTL, g_rsxr_target_ALTR;
void Rsxr_Mid_COAT(RsxrMid::Context&);  // coop.cpp
void Rsxr_Mid_COAS(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_COAT, g_rsxr_resume_COAS;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("ALTS", &Rsxr_Mid_ALTS, &g_rsxr_resume_ALTS),
    Rsxr_Mid("ALTL", &Rsxr_Mid_ALTL, nullptr, &g_rsxr_target_ALTL),
    Rsxr_Mid("ALTF", &Rsxr_Mid_ALTF, nullptr, &g_rsxr_target_ALTF).After(),
    Rsxr_Mid("ALTR", &Rsxr_Mid_ALTR, nullptr, &g_rsxr_target_ALTR).After(),
    Rsxr_Mid("COAT", &Rsxr_Mid_COAT, &g_rsxr_resume_COAT),
    Rsxr_Mid("COAS", &Rsxr_Mid_COAS, &g_rsxr_resume_COAS),
};
}  // namespace

extern const RsxrHookTable kRsxrArrangementHooks = {Rsxr_Group(RsxrFeature::Core), kHooks,
                                                    sizeof kHooks / sizeof kHooks[0]};
