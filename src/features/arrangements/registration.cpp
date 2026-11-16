// The arrangement page hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Group Core:
// nothing changes for songs with five (co-op: eight) or fewer arrangements.
#include "features/arrangements/arrangements.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_ALTS(Rs7Mid::Context&);  // hooks.cpp
void Rs7_Mid_ALTF(Rs7Mid::Context&);
void Rs7_Mid_ALTL(Rs7Mid::Context&);
void Rs7_Mid_ALTR(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_ALTS, g_rs7_target_ALTF, g_rs7_target_ALTL, g_rs7_target_ALTR;
void Rs7_Mid_COAT(Rs7Mid::Context&);  // coop.cpp
void Rs7_Mid_COAS(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_COAT, g_rs7_resume_COAS;
}

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("ALTS", &Rs7_Mid_ALTS, &g_rs7_resume_ALTS),
    Rs7_Mid("ALTL", &Rs7_Mid_ALTL, nullptr, &g_rs7_target_ALTL),
    Rs7_Mid("ALTF", &Rs7_Mid_ALTF, nullptr, &g_rs7_target_ALTF).After(),
    Rs7_Mid("ALTR", &Rs7_Mid_ALTR, nullptr, &g_rs7_target_ALTR).After(),
    Rs7_Mid("COAT", &Rs7_Mid_COAT, &g_rs7_resume_COAT),
    Rs7_Mid("COAS", &Rs7_Mid_COAS, &g_rs7_resume_COAS),
};
}  // namespace

extern const Rs7HookTable kRs7ArrangementHooks = {Rs7_Group(Rs7Feature::Core), kHooks,
                                                  sizeof kHooks / sizeof kHooks[0]};
