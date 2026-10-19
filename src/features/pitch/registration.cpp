// The pitch feature's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Two tables,
// groups as before the move: the native pitch storage (group Pitch; the rows keep the `pitch` flag, which
// Rs7_ShouldPatch reads) and the song's 7-string mode S1 S2 S3 I1 S4 (base 7-string sites, group Core).
#include "features/pitch/pitch.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_PE0(Rs7Mid::Context&);  // hooks_mid.cpp, light
void Rs7_Mid_PM0(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_PR0(Rs7Mid::Context&);
void Rs7_Mid_PM1(Rs7Mid::Context&);
void Rs7_Mid_PT1(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_PM0, g_rs7_resume_PE0;
extern uintptr_t g_rs7_target_PC1, g_rs7_target_PC2, g_rs7_target_PC3, g_rs7_target_PC4;
void Rs7_Mid_PC1(Rs7Mid::Context&);  // hooks_calls.cpp, PC1 / PC4 light
void Rs7_Mid_PC2(Rs7Mid::Context&);
void Rs7_Mid_PC4(Rs7Mid::Context&);
void Rs7_Mid_PC3(Rs7Mid::Context&);
void Rs7_Stub_S1();
void Rs7_Stub_S2();
void Rs7_Mid_S3(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_I1(Rs7Mid::Context&);
void Rs7_Mid_S4(Rs7Mid::Context&);  // hooks_calls.cpp
extern uintptr_t g_rs7_target_S1, g_rs7_resume_S2, g_rs7_target_S2, g_rs7_resume_I1, g_rs7_target_S4;
}

namespace {
const Rs7Hook kHooks[] = {
// [neck] native_pitch: native 8-entry tuning / open-MIDI storage (pitch.h, issue #18 stage 5 area 3).
#define RS7_PITCH(id, stub, res, tgt) {id, stub, res, tgt, false, false, false, 0, 0, nullptr, 0, kRs7PitchNative}
#define RS7_PITCH_MID(id, res, tgt, mid) {id, nullptr, res, tgt, false, false, false, 0, 0, mid, 0, kRs7PitchNative}
#define RS7_PITCH_LIGHT(id, res, tgt, mid)                                                                             \
    {id, nullptr, res, tgt, false, false, false, 0, 0, mid, 0, kRs7PitchNative, 0, false, Rs7Mid::Mode::Light}
    RS7_PITCH("PA1", nullptr, nullptr, nullptr),
    RS7_PITCH("PA2", nullptr, nullptr, nullptr),
    RS7_PITCH_MID("PM0", &g_rs7_resume_PM0, nullptr, &Rs7_Mid_PM0),
    RS7_PITCH_MID("PR0", nullptr, nullptr, &Rs7_Mid_PR0),
    RS7_PITCH_MID("PT1", nullptr, nullptr, &Rs7_Mid_PT1),
    RS7_PITCH_MID("PM1", nullptr, nullptr, &Rs7_Mid_PM1),
    RS7_PITCH_LIGHT("PE0", &g_rs7_resume_PE0, nullptr, &Rs7_Mid_PE0),
    RS7_PITCH_LIGHT("PC1", nullptr, &g_rs7_target_PC1, &Rs7_Mid_PC1),
    RS7_PITCH_MID("PC2", nullptr, &g_rs7_target_PC2, &Rs7_Mid_PC2),
    RS7_PITCH_MID("PC3", nullptr, &g_rs7_target_PC3, &Rs7_Mid_PC3),
    RS7_PITCH_LIGHT("PC4", nullptr, &g_rs7_target_PC4, &Rs7_Mid_PC4),
#undef RS7_PITCH_LIGHT
#undef RS7_PITCH_MID
#undef RS7_PITCH
};

const Rs7Hook kCoreHooks[] = {
    {"S1", reinterpret_cast<void*>(&Rs7_Stub_S1), nullptr, &g_rs7_target_S1, false, false},
    {"S2", reinterpret_cast<void*>(&Rs7_Stub_S2), &g_rs7_resume_S2, &g_rs7_target_S2, false, false},
    {"S3", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_S3},
    {"I1", nullptr, &g_rs7_resume_I1, nullptr, false, false, false, 0, 0, &Rs7_Mid_I1},
    {"S4", nullptr, nullptr, &g_rs7_target_S4, false, false, false, 0, 0, &Rs7_Mid_S4},
};
}  // namespace

extern const Rs7HookTable kRs7PitchHooks = {Rs7_Group(Rs7Feature::Pitch), kHooks, sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7PitchCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                sizeof kCoreHooks / sizeof kCoreHooks[0]};
