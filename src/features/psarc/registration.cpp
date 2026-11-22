// The plain-PSARC unit's hooks (sites.json group "psarc"): looked up by src/registry.cpp (Rsxr_FindHook) and
// written by RsxrPlain_Start (hooks.cpp), not by the main batch.
#include "features/psarc/psarc.h"
#include "model/groups.h"
#include <cstdint>

extern "C" {
void Rsxr_Mid_PP_TOC(RsxrMid::Context&);  // hooks_mid.cpp
void Rsxr_Mid_PP_SNGF(RsxrMid::Context&);
bool __cdecl Rsxr_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);
extern uintptr_t g_rsxr_resume_PP_SNGF, g_rsxr_target_PP_SNGD;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Stub("PP_SNGD", reinterpret_cast<void*>(&Rsxr_Stub_PP_SNGD), nullptr, &g_rsxr_target_PP_SNGD),
    Rsxr_Mid("PP_SNGF", &Rsxr_Mid_PP_SNGF, &g_rsxr_resume_PP_SNGF),
    Rsxr_Mid("PP_TOC", &Rsxr_Mid_PP_TOC),
};
}  // namespace

extern const RsxrHookTable kRsxrPsarcHooks = {Rsxr_Group(RsxrFeature::Psarc), kHooks, sizeof kHooks / sizeof kHooks[0]};
