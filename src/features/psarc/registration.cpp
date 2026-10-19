// The plain-PSARC unit's hooks (sites.json group "psarc"): looked up by src/registry.cpp (Rs7_FindHook) and
// written by Rs7Plain_Start (hooks.cpp), not by the main batch.
#include "features/psarc/psarc.h"
#include "model/groups.h"
#include <cstdint>

extern "C" {
void Rs7_Mid_PP_TOC(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_PP_SNGF(Rs7Mid::Context&);
bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);
extern uintptr_t g_rs7_resume_PP_SNGF, g_rs7_target_PP_SNGD;
}

namespace {
const Rs7Hook kHooks[] = {
    {"PP_SNGD", reinterpret_cast<void*>(&Rs7_Stub_PP_SNGD), nullptr, &g_rs7_target_PP_SNGD, false, false},
    {"PP_SNGF", nullptr, &g_rs7_resume_PP_SNGF, nullptr, false, false, false, 0, 0, &Rs7_Mid_PP_SNGF},
    {"PP_TOC", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_PP_TOC},
};
}  // namespace

extern const Rs7HookTable kRs7PsarcHooks = {Rs7_Group(Rs7Feature::Psarc), kHooks, sizeof kHooks / sizeof kHooks[0]};
