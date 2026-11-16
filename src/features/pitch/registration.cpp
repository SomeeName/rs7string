// The pitch feature's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Two tables,
// groups as before the move: the native pitch storage (group Pitch; the rows keep the `pitch` flag, which
// Rs7_ShouldPatch reads) and the song's 7-string mode S1 S3 I1 S4 (base 7-string sites, group Core).
#include "features/pitch/pitch.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_PE0(Rs7Mid::Context&);  // hooks_mid.cpp, light
void Rs7_Mid_PM0(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_PR0(Rs7Mid::Context&);
void Rs7_Mid_PM1(Rs7Mid::Context&);
void Rs7_Mid_PT1(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_PM0, g_rs7_resume_PE0;
extern uintptr_t g_rs7_target_PC1, g_rs7_target_PC2, g_rs7_target_PC3, g_rs7_target_PC4, g_rs7_target_PBH;
void Rs7_Mid_PC1(Rs7Mid::Context&);  // hooks_calls.cpp, PC1 / PC4 light
void Rs7_Mid_PC2(Rs7Mid::Context&);
void Rs7_Mid_PC4(Rs7Mid::Context&);
void Rs7_Mid_PC3(Rs7Mid::Context&);
void Rs7_Mid_PBH(Rs7Mid::Context&);  // hooks_calls.cpp, After
void Rs7_Stub_S1();
void Rs7_Mid_S3(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_I1(Rs7Mid::Context&);
void Rs7_Mid_S4(Rs7Mid::Context&);  // hooks_calls.cpp
extern uintptr_t g_rs7_target_S1, g_rs7_resume_I1, g_rs7_target_S4;
}

namespace {
const Rs7Hook kHooks[] = {
    // Native 8-entry tuning / open-MIDI storage (pitch.h).
    Rs7_Site("PA1").Gate(kRs7GatePitch),
    Rs7_Site("PA2").Gate(kRs7GatePitch),
    Rs7_Mid("PM0", &Rs7_Mid_PM0, &g_rs7_resume_PM0).Gate(kRs7GatePitch),
    Rs7_Mid("PR0", &Rs7_Mid_PR0).Gate(kRs7GatePitch),
    Rs7_Mid("PT1", &Rs7_Mid_PT1).Gate(kRs7GatePitch),
    Rs7_Mid("PM1", &Rs7_Mid_PM1).Gate(kRs7GatePitch),
    Rs7_Mid("PE0", &Rs7_Mid_PE0, &g_rs7_resume_PE0).Gate(kRs7GatePitch).Light(),
    Rs7_Mid("PC1", &Rs7_Mid_PC1, nullptr, &g_rs7_target_PC1).Gate(kRs7GatePitch).Light(),
    Rs7_Mid("PC2", &Rs7_Mid_PC2, nullptr, &g_rs7_target_PC2).Gate(kRs7GatePitch),
    Rs7_Mid("PC3", &Rs7_Mid_PC3, nullptr, &g_rs7_target_PC3).Gate(kRs7GatePitch),
    Rs7_Mid("PC4", &Rs7_Mid_PC4, nullptr, &g_rs7_target_PC4).Gate(kRs7GatePitch).Light(),
    Rs7_Mid("PBH", &Rs7_Mid_PBH, nullptr, &g_rs7_target_PBH).Gate(kRs7GatePitch).After(),
};

const Rs7Hook kCoreHooks[] = {
    Rs7_Stub("S1", reinterpret_cast<void*>(&Rs7_Stub_S1), nullptr, &g_rs7_target_S1),
    Rs7_Mid("S3", &Rs7_Mid_S3),
    Rs7_Mid("I1", &Rs7_Mid_I1, &g_rs7_resume_I1),
    Rs7_Mid("S4", &Rs7_Mid_S4, nullptr, &g_rs7_target_S4),
};
}  // namespace

extern const Rs7HookTable kRs7PitchHooks = {Rs7_Group(Rs7Feature::Pitch), kHooks, sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7PitchCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                sizeof kCoreHooks / sizeof kCoreHooks[0]};
