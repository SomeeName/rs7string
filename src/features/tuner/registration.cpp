#include "features/tuner/hooks.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8s_ids.generated.h"
#include "model/gates.h"
#include "model/groups.h"

// The tuner's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.

extern "C" {
#define RS7_TS8_DECL(ID, IDX) void Rs7_Mid_##ID(Rs7Mid::Context&);
RS7_TU8S_SPECS(RS7_TS8_DECL)  // hooks_entries.cpp
#undef RS7_TS8_DECL
void Rs7_Mid_TU8H(Rs7Mid::Context&);
void Rs7_Mid_TU8C(Rs7Mid::Context&);
void Rs7_Mid_TU8A(Rs7Mid::Context&);
void Rs7_Mid_TU8INIT(Rs7Mid::Context&);
void Rs7_Mid_TU8FREE(Rs7Mid::Context&);
void Rs7_Mid_AT(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_TU8FREE;
void Rs7_Mid_TF1(Rs7Mid::Context&);  // hooks_camera.cpp
void Rs7_Mid_TF2(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_TF1;
}

// A native-session row: written only with the native tuner and its prerequisites (Rs7_ShouldPatch).
// clang-format off
// clang-format on
#define RS7_TS8_ROW(ID, IDX) Rs7_Mid(#ID, &Rs7_Mid_##ID).Gate(kRs7GateTuner),
#define RS7_TS8_MID(ID) Rs7_Mid(#ID, &Rs7_Mid_##ID).Gate(kRs7GateTuner),

namespace {
// The native 8-slot tuner session (group Tuner): GuitarStrings readers, the grown session, the method copies
// (tools/gen_tuner8s.py) and the mid-hooks in the uncopied methods.
const Rs7Hook kSessionHooks[] = {Rs7_Mid("TU8H", &Rs7_Mid_TU8H).Gate(kRs7GateTuner),
                                 Rs7_Mid("TU8C", &Rs7_Mid_TU8C).Gate(kRs7GateTuner),
                                 Rs7_Mid("TU8A", &Rs7_Mid_TU8A).Gate(kRs7GateTuner),
                                 Rs7_Site("TU8ALLOC").Gate(kRs7GateTuner),
                                 Rs7_Mid("TU8INIT", &Rs7_Mid_TU8INIT).Gate(kRs7GateTuner),
                                 Rs7_Mid("TU8FREE", &Rs7_Mid_TU8FREE, &g_rs7_resume_TU8FREE).Gate(kRs7GateTuner),
                                 RS7_TU8S_SPECS(RS7_TS8_ROW)  // TS8SEL ... TS8HNT
                                 RS7_TS8_MID(TS8F1) RS7_TS8_MID(TS8F2) RS7_TS8_MID(TS8F3) RS7_TS8_MID(TS8RS)
                                     RS7_TS8_MID(TS8DT) RS7_TS8_MID(TS8WR) RS7_TS8_MID(TS8SN1) RS7_TS8_MID(TS8SN2)
                                         RS7_TS8_MID(TS8PA) RS7_TS8_MID(TS8PZ)};

// The shared tuner hooks and the tuning names with their refs (group Core, as before the move).
const Rs7Hook kCoreHooks[] = {
    Rs7_Ref("X_TMGR", &g_rs7_tuner_mgr),
    Rs7_Ref("X_GETDET", &g_rs7_fn_getdet),
    Rs7_Mid("TK", &Rs7_Mid_TK),
    Rs7_Mid("TS", &Rs7_Mid_TS),
    Rs7_Ref("X_WTHR", &g_rs7_wrong_const),
    Rs7_Mid("W", &Rs7_Mid_W),
    Rs7_Mid("AT", &Rs7_Mid_AT),
    Rs7_WrapRow(Rs7_Site("TJ"), kRs7WrapTJ, &Rs7_Mid_TJBefore, &Rs7_Mid_TJ),
    Rs7_WrapRow(Rs7_Site("TU"), kRs7WrapTU, &Rs7_Mid_TUBefore, &Rs7_Mid_TU),
    Rs7_Mid("TUT", &Rs7_Mid_TUT),
    Rs7_WrapRow(Rs7_Site("TN"), kRs7WrapTN, &Rs7_Mid_TNBefore, &Rs7_Mid_TN),
    Rs7_Ref("X_STRCON", &g_rs7_fn_strcon),
    Rs7_Ref("X_STRASG", &g_rs7_fn_strasg),
    Rs7_Ref("X_LOC", &g_rs7_fn_loc),
    Rs7_Ref("X_STRDTOR", &g_rs7_fn_strdtor),
    Rs7_Mid("TF1", &Rs7_Mid_TF1, &g_rs7_resume_TF1).Light(),
    Rs7_Mid("TF2", &Rs7_Mid_TF2),
};
}  // namespace

#undef RS7_TS8_MID
#undef RS7_TS8_ROW

extern const Rs7HookTable kRs7TunerHooks = {Rs7_Group(Rs7Feature::Tuner), kSessionHooks,
                                            sizeof kSessionHooks / sizeof kSessionHooks[0]};
extern const Rs7HookTable kRs7TunerCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                sizeof kCoreHooks / sizeof kCoreHooks[0]};
