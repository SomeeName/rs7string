// The per-guitar 7-string look's hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch.
// Group Core, as before the move: N1 N5 N12 N13 N18-N21 are base 7-string sites, the X_* rows data references (game
// functions the native strings, pegs, frets and headstock model call), R3a-R3c R4 the string-6 lane of note effects, R2
// R6 of note heads and gems, BD1-BD4 string 6's bend direction.
#include "features/string7/string7.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_N1(RsxrMid::Context&);
void Rsxr_Mid_N21(RsxrMid::Context&);
void Rsxr_Mid_N12(RsxrMid::Context&);
void Rsxr_Mid_N13(RsxrMid::Context&);
void Rsxr_Mid_N5(RsxrMid::Context&);
void Rsxr_Mid_N18(RsxrMid::Context&);
void Rsxr_Mid_N19(RsxrMid::Context&);
void Rsxr_Mid_N20(RsxrMid::Context&);
void Rsxr_Mid_R3a(RsxrMid::Context&);  // hooks_lanes.cpp
void Rsxr_Mid_R3b(RsxrMid::Context&);
void Rsxr_Mid_R3c(RsxrMid::Context&);
void Rsxr_Mid_R4(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_N1, g_rsxr_resume_N21, g_rsxr_resume_N12;
extern uintptr_t g_rsxr_resume_N18, g_rsxr_resume_N19, g_rsxr_resume_N20;
extern uintptr_t g_rsxr_target_N18, g_rsxr_target_N19, g_rsxr_target_N20;
extern uintptr_t g_rsxr_resume_R3a, g_rsxr_resume_R3b, g_rsxr_resume_R3c, g_rsxr_resume_R4;
void Rsxr_Mid_R2(RsxrMid::Context&);  // hooks_lanes.cpp, sse mode
void Rsxr_Mid_R6(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_R2, g_rsxr_resume_R6;
void Rsxr_Mid_BD1(RsxrMid::Context&);  // hooks_benddir.cpp
void Rsxr_Mid_BD2(RsxrMid::Context&);
void Rsxr_Mid_BD3(RsxrMid::Context&);
void Rsxr_Mid_BD4(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_BD1, g_rsxr_target_BD1, g_rsxr_resume_BD2, g_rsxr_target_BD2;
extern uintptr_t g_rsxr_resume_BD3, g_rsxr_target_BD3, g_rsxr_resume_BD4, g_rsxr_target_BD4;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("N1", &Rsxr_Mid_N1, &g_rsxr_resume_N1),
    Rsxr_Mid("N5", &Rsxr_Mid_N5),
    Rsxr_Ref("X_ALLOC", &g_rsxr_fn_alloc),
    Rsxr_Ref("X_STRCTOR", &g_rsxr_fn_strctor),
    Rsxr_Ref("X_SHAPE", &g_rsxr_fn_shape),
    Rsxr_Ref("X_SHOW", &g_rsxr_fn_show),
    Rsxr_Ref("X_SETPOS", &g_rsxr_fn_setpos),
    Rsxr_Ref("X_SETROT", &g_rsxr_fn_setrot),
    Rsxr_Ref("X_DT", &g_rsxr_frame_global),
    Rsxr_Mid("N12", &Rsxr_Mid_N12, &g_rsxr_resume_N12),
    Rsxr_Mid("N13", &Rsxr_Mid_N13),
    Rsxr_Mid("N18", &Rsxr_Mid_N18, &g_rsxr_resume_N18, &g_rsxr_target_N18),
    Rsxr_Mid("N19", &Rsxr_Mid_N19, &g_rsxr_resume_N19, &g_rsxr_target_N19).Light(),
    Rsxr_Mid("N20", &Rsxr_Mid_N20, &g_rsxr_resume_N20, &g_rsxr_target_N20).Light(),
    Rsxr_Mid("N21", &Rsxr_Mid_N21, &g_rsxr_resume_N21),
    Rsxr_Ref("X_DETACH", &g_rsxr_fn_detach),
    // string-6 lane position of spawned note effects (hooks_lanes.cpp)
    Rsxr_Mid("R3a", &Rsxr_Mid_R3a, &g_rsxr_resume_R3a),
    Rsxr_Mid("R3b", &Rsxr_Mid_R3b, &g_rsxr_resume_R3b),
    Rsxr_Mid("R3c", &Rsxr_Mid_R3c, &g_rsxr_resume_R3c),
    Rsxr_Mid("R4", &Rsxr_Mid_R4, &g_rsxr_resume_R4),
    // string-6 lane z of note heads and lane offset y of gems (hooks_lanes.cpp; per note and frame: sse mode)
    Rsxr_Mid("R2", &Rsxr_Mid_R2, &g_rsxr_resume_R2).Sse(),
    Rsxr_Mid("R6", &Rsxr_Mid_R6, &g_rsxr_resume_R6).Sse(),
    // string 6's bend direction (hooks_benddir.cpp)
    Rsxr_Mid("BD1", &Rsxr_Mid_BD1, &g_rsxr_resume_BD1, &g_rsxr_target_BD1).Light(),
    Rsxr_Mid("BD2", &Rsxr_Mid_BD2, &g_rsxr_resume_BD2, &g_rsxr_target_BD2).Light(),
    Rsxr_Mid("BD3", &Rsxr_Mid_BD3, &g_rsxr_resume_BD3, &g_rsxr_target_BD3).Light(),
    Rsxr_Mid("BD4", &Rsxr_Mid_BD4, &g_rsxr_resume_BD4, &g_rsxr_target_BD4).Light(),
};
}  // namespace

extern const RsxrHookTable kRsxrString7Hooks = {Rsxr_Group(RsxrFeature::Core), kHooks,
                                                sizeof kHooks / sizeof kHooks[0]};
