// The per-guitar 7-string look's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.
// Group Core, as before the move: N1 N5 N12 N13 N18-N23 are base 7-string sites, the X_* rows data references (game
// functions the native strings, pegs, frets and headstock model call), R3a-R3c R4 the string-6 lane of note effects, R2
// R6 of note heads and gems, BD1-BD4 string 6's bend direction.
#include "features/string7/string7.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_N1(Rs7Mid::Context&);
void Rs7_Mid_N21(Rs7Mid::Context&);
void Rs7_Mid_N12(Rs7Mid::Context&);
void Rs7_Mid_N13(Rs7Mid::Context&);
void Rs7_Mid_N5(Rs7Mid::Context&);
void Rs7_Mid_N18(Rs7Mid::Context&);
void Rs7_Mid_N19(Rs7Mid::Context&);
void Rs7_Mid_N20(Rs7Mid::Context&);
void Rs7_Mid_N22(Rs7Mid::Context&);
void Rs7_Mid_N23(Rs7Mid::Context&);
void Rs7_Mid_R3a(Rs7Mid::Context&);  // hooks_lanes.cpp
void Rs7_Mid_R3b(Rs7Mid::Context&);
void Rs7_Mid_R3c(Rs7Mid::Context&);
void Rs7_Mid_R4(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_N1, g_rs7_resume_N21, g_rs7_resume_N12;
extern uintptr_t g_rs7_resume_N18, g_rs7_resume_N19, g_rs7_resume_N20, g_rs7_resume_N22, g_rs7_resume_N23;
extern uintptr_t g_rs7_resume_R3a, g_rs7_resume_R3b, g_rs7_resume_R3c, g_rs7_resume_R4;
void Rs7_Mid_R2(Rs7Mid::Context&);  // hooks_lanes.cpp, sse mode
void Rs7_Mid_R6(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_R2, g_rs7_resume_R6;
void Rs7_Mid_BD1(Rs7Mid::Context&);  // hooks_benddir.cpp
void Rs7_Mid_BD2(Rs7Mid::Context&);
void Rs7_Mid_BD3(Rs7Mid::Context&);
void Rs7_Mid_BD4(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_BD1, g_rs7_target_BD1, g_rs7_resume_BD2, g_rs7_target_BD2;
extern uintptr_t g_rs7_resume_BD3, g_rs7_target_BD3, g_rs7_resume_BD4, g_rs7_target_BD4;
}

namespace {
const Rs7Hook kHooks[] = {
    {"N1", nullptr, &g_rs7_resume_N1, nullptr, false, false, false, 0, 0, &Rs7_Mid_N1},
    {"N5", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_N5},
    {"X_ALLOC", nullptr, nullptr, &g_rs7_fn_alloc, false, true},
    {"X_STRCTOR", nullptr, nullptr, &g_rs7_fn_strctor, false, true},
    {"X_SHAPE", nullptr, nullptr, &g_rs7_fn_shape, false, true},
    {"X_SHOW", nullptr, nullptr, &g_rs7_fn_show, false, true},
    {"X_SETPOS", nullptr, nullptr, &g_rs7_fn_setpos, false, true},
    {"X_SETROT", nullptr, nullptr, &g_rs7_fn_setrot, false, true},
    {"X_DT", nullptr, nullptr, &g_rs7_frame_global, false, true},
    {"N12", nullptr, &g_rs7_resume_N12, nullptr, false, false, false, 0, 0, &Rs7_Mid_N12},
    {"N13", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_N13},
    {"N18", nullptr, &g_rs7_resume_N18, nullptr, false, false, false, 0, 0, &Rs7_Mid_N18},
    {"N19", nullptr, &g_rs7_resume_N19, nullptr, false, false, false, 0, 0, &Rs7_Mid_N19, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    {"N20", nullptr, &g_rs7_resume_N20, nullptr, false, false, false, 0, 0, &Rs7_Mid_N20, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    {"N21", nullptr, &g_rs7_resume_N21, nullptr, false, false, false, 0, 0, &Rs7_Mid_N21},
    {"X_DETACH", nullptr, nullptr, &g_rs7_fn_detach, false, true},
    {"N22", nullptr, &g_rs7_resume_N22, nullptr, false, false, false, 0, 0, &Rs7_Mid_N22},
    {"N23", nullptr, &g_rs7_resume_N23, nullptr, false, false, false, 0, 0, &Rs7_Mid_N23, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    // string-6 lane position of spawned note effects (hooks_lanes.cpp)
    {"R3a", nullptr, &g_rs7_resume_R3a, nullptr, false, false, false, 0, 0, &Rs7_Mid_R3a},
    {"R3b", nullptr, &g_rs7_resume_R3b, nullptr, false, false, false, 0, 0, &Rs7_Mid_R3b},
    {"R3c", nullptr, &g_rs7_resume_R3c, nullptr, false, false, false, 0, 0, &Rs7_Mid_R3c},
    {"R4", nullptr, &g_rs7_resume_R4, nullptr, false, false, false, 0, 0, &Rs7_Mid_R4},
    // string-6 lane z of note heads and lane offset y of gems (hooks_lanes.cpp; per note and frame: sse mode)
    {"R2", nullptr, &g_rs7_resume_R2, nullptr, false, false, false, 0, 0, &Rs7_Mid_R2, 0, 0, 0, false,
     Rs7Mid::Mode::Sse},
    {"R6", nullptr, &g_rs7_resume_R6, nullptr, false, false, false, 0, 0, &Rs7_Mid_R6, 0, 0, 0, false,
     Rs7Mid::Mode::Sse},
    // string 6's bend direction (hooks_benddir.cpp)
    {"BD1", nullptr, &g_rs7_resume_BD1, &g_rs7_target_BD1, false, false, false, 0, 0, &Rs7_Mid_BD1, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    {"BD2", nullptr, &g_rs7_resume_BD2, &g_rs7_target_BD2, false, false, false, 0, 0, &Rs7_Mid_BD2, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    {"BD3", nullptr, &g_rs7_resume_BD3, &g_rs7_target_BD3, false, false, false, 0, 0, &Rs7_Mid_BD3, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
    {"BD4", nullptr, &g_rs7_resume_BD4, &g_rs7_target_BD4, false, false, false, 0, 0, &Rs7_Mid_BD4, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
};
}  // namespace

extern const Rs7HookTable kRs7String7Hooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
