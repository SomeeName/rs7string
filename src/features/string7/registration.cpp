// The per-guitar 7-string look's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.
// Group Core, as before the move: N1 N5 N12 N13 N18-N21 are base 7-string sites, the X_* rows data references (game
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
void Rs7_Mid_R3a(Rs7Mid::Context&);  // hooks_lanes.cpp
void Rs7_Mid_R3b(Rs7Mid::Context&);
void Rs7_Mid_R3c(Rs7Mid::Context&);
void Rs7_Mid_R4(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_N1, g_rs7_resume_N21, g_rs7_resume_N12;
extern uintptr_t g_rs7_resume_N18, g_rs7_resume_N19, g_rs7_resume_N20;
extern uintptr_t g_rs7_target_N18, g_rs7_target_N19, g_rs7_target_N20;
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
    Rs7_Mid("N1", &Rs7_Mid_N1, &g_rs7_resume_N1),
    Rs7_Mid("N5", &Rs7_Mid_N5),
    Rs7_Ref("X_ALLOC", &g_rs7_fn_alloc),
    Rs7_Ref("X_STRCTOR", &g_rs7_fn_strctor),
    Rs7_Ref("X_SHAPE", &g_rs7_fn_shape),
    Rs7_Ref("X_SHOW", &g_rs7_fn_show),
    Rs7_Ref("X_SETPOS", &g_rs7_fn_setpos),
    Rs7_Ref("X_SETROT", &g_rs7_fn_setrot),
    Rs7_Ref("X_DT", &g_rs7_frame_global),
    Rs7_Mid("N12", &Rs7_Mid_N12, &g_rs7_resume_N12),
    Rs7_Mid("N13", &Rs7_Mid_N13),
    Rs7_Mid("N18", &Rs7_Mid_N18, &g_rs7_resume_N18, &g_rs7_target_N18),
    Rs7_Mid("N19", &Rs7_Mid_N19, &g_rs7_resume_N19, &g_rs7_target_N19).Light(),
    Rs7_Mid("N20", &Rs7_Mid_N20, &g_rs7_resume_N20, &g_rs7_target_N20).Light(),
    Rs7_Mid("N21", &Rs7_Mid_N21, &g_rs7_resume_N21),
    Rs7_Ref("X_DETACH", &g_rs7_fn_detach),
    // string-6 lane position of spawned note effects (hooks_lanes.cpp)
    Rs7_Mid("R3a", &Rs7_Mid_R3a, &g_rs7_resume_R3a),
    Rs7_Mid("R3b", &Rs7_Mid_R3b, &g_rs7_resume_R3b),
    Rs7_Mid("R3c", &Rs7_Mid_R3c, &g_rs7_resume_R3c),
    Rs7_Mid("R4", &Rs7_Mid_R4, &g_rs7_resume_R4),
    // string-6 lane z of note heads and lane offset y of gems (hooks_lanes.cpp; per note and frame: sse mode)
    Rs7_Mid("R2", &Rs7_Mid_R2, &g_rs7_resume_R2).Sse(),
    Rs7_Mid("R6", &Rs7_Mid_R6, &g_rs7_resume_R6).Sse(),
    // string 6's bend direction (hooks_benddir.cpp)
    Rs7_Mid("BD1", &Rs7_Mid_BD1, &g_rs7_resume_BD1, &g_rs7_target_BD1).Light(),
    Rs7_Mid("BD2", &Rs7_Mid_BD2, &g_rs7_resume_BD2, &g_rs7_target_BD2).Light(),
    Rs7_Mid("BD3", &Rs7_Mid_BD3, &g_rs7_resume_BD3, &g_rs7_target_BD3).Light(),
    Rs7_Mid("BD4", &Rs7_Mid_BD4, &g_rs7_resume_BD4, &g_rs7_target_BD4).Light(),
};
}  // namespace

extern const Rs7HookTable kRs7String7Hooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
