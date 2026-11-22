// The pitch feature's hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch. Two tables,
// groups as before the move: the native pitch storage (group Pitch; the rows keep the `pitch` flag, which
// Rsxr_ShouldPatch reads) and the song's 7-string mode S1 S3 I1 S4 (base 7-string sites, group Core).
#include "features/pitch/pitch.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_PE0(RsxrMid::Context&);  // hooks_mid.cpp, light
void Rsxr_Mid_PM0(RsxrMid::Context&);  // hooks_mid.cpp
void Rsxr_Mid_PR0(RsxrMid::Context&);
void Rsxr_Mid_PM1(RsxrMid::Context&);
void Rsxr_Mid_PT1(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_PM0, g_rsxr_resume_PE0;
extern uintptr_t g_rsxr_target_PC1, g_rsxr_target_PC2, g_rsxr_target_PC3, g_rsxr_target_PC4, g_rsxr_target_PBH;
void Rsxr_Mid_PC1(RsxrMid::Context&);  // hooks_calls.cpp, PC1 / PC4 light
void Rsxr_Mid_PC2(RsxrMid::Context&);
void Rsxr_Mid_PC4(RsxrMid::Context&);
void Rsxr_Mid_PC3(RsxrMid::Context&);
void Rsxr_Mid_PBH(RsxrMid::Context&);  // hooks_calls.cpp, After
void Rsxr_Stub_S1();
void Rsxr_Mid_S3(RsxrMid::Context&);  // hooks_mid.cpp
void Rsxr_Mid_I1(RsxrMid::Context&);
void Rsxr_Mid_S4(RsxrMid::Context&);  // hooks_calls.cpp
extern uintptr_t g_rsxr_target_S1, g_rsxr_resume_I1, g_rsxr_target_S4;
}

namespace {
const RsxrHook kHooks[] = {
    // Native 8-entry tuning / open-MIDI storage (pitch.h).
    Rsxr_Site("PA1").Gate(kRsxrGatePitch),
    Rsxr_Site("PA2").Gate(kRsxrGatePitch),
    Rsxr_Mid("PM0", &Rsxr_Mid_PM0, &g_rsxr_resume_PM0).Gate(kRsxrGatePitch),
    Rsxr_Mid("PR0", &Rsxr_Mid_PR0).Gate(kRsxrGatePitch),
    Rsxr_Mid("PT1", &Rsxr_Mid_PT1).Gate(kRsxrGatePitch),
    Rsxr_Mid("PM1", &Rsxr_Mid_PM1).Gate(kRsxrGatePitch),
    Rsxr_Mid("PE0", &Rsxr_Mid_PE0, &g_rsxr_resume_PE0).Gate(kRsxrGatePitch).Light(),
    Rsxr_Mid("PC1", &Rsxr_Mid_PC1, nullptr, &g_rsxr_target_PC1).Gate(kRsxrGatePitch).Light(),
    Rsxr_Mid("PC2", &Rsxr_Mid_PC2, nullptr, &g_rsxr_target_PC2).Gate(kRsxrGatePitch),
    Rsxr_Mid("PC3", &Rsxr_Mid_PC3, nullptr, &g_rsxr_target_PC3).Gate(kRsxrGatePitch),
    Rsxr_Mid("PC4", &Rsxr_Mid_PC4, nullptr, &g_rsxr_target_PC4).Gate(kRsxrGatePitch).Light(),
    Rsxr_Mid("PBH", &Rsxr_Mid_PBH, nullptr, &g_rsxr_target_PBH).Gate(kRsxrGatePitch).After(),
};

const RsxrHook kCoreHooks[] = {
    Rsxr_Stub("S1", reinterpret_cast<void*>(&Rsxr_Stub_S1), nullptr, &g_rsxr_target_S1),
    Rsxr_Mid("S3", &Rsxr_Mid_S3),
    Rsxr_Mid("I1", &Rsxr_Mid_I1, &g_rsxr_resume_I1),
    Rsxr_Mid("S4", &Rsxr_Mid_S4, nullptr, &g_rsxr_target_S4),
};
}  // namespace

extern const RsxrHookTable kRsxrPitchHooks = {Rsxr_Group(RsxrFeature::Pitch), kHooks, sizeof kHooks / sizeof kHooks[0]};
extern const RsxrHookTable kRsxrPitchCoreHooks = {Rsxr_Group(RsxrFeature::Core), kCoreHooks,
                                                  sizeof kCoreHooks / sizeof kCoreHooks[0]};
