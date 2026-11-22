#include "features/tuner/hooks.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8s_ids.generated.h"
#include "model/gates.h"
#include "model/groups.h"

// The tuner's hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch.

extern "C" {
#define RSXR_TS8_DECL(ID, IDX) void Rsxr_Mid_##ID(RsxrMid::Context&);
RSXR_TU8S_SPECS(RSXR_TS8_DECL)  // hooks_entries.cpp
#undef RSXR_TS8_DECL
void Rsxr_Mid_TU8H(RsxrMid::Context&);
void Rsxr_Mid_TU8C(RsxrMid::Context&);
void Rsxr_Mid_TU8A(RsxrMid::Context&);
void Rsxr_Mid_TU8INIT(RsxrMid::Context&);
void Rsxr_Mid_TU8FREE(RsxrMid::Context&);
void Rsxr_Mid_AT(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_TU8FREE;
void Rsxr_Mid_TF1(RsxrMid::Context&);  // hooks_camera.cpp
void Rsxr_Mid_TF2(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_TF1;
}

// A native-session row: written only with the native tuner and its prerequisites (Rsxr_ShouldPatch).
// clang-format off
// clang-format on
#define RSXR_TS8_ROW(ID, IDX) Rsxr_Mid(#ID, &Rsxr_Mid_##ID).Gate(kRsxrGateTuner),
#define RSXR_TS8_MID(ID) Rsxr_Mid(#ID, &Rsxr_Mid_##ID).Gate(kRsxrGateTuner),

namespace {
// The native 8-slot tuner session (group Tuner): GuitarStrings readers, the grown session, the method copies
// (tools/gen_tuner8s.py) and the mid-hooks in the uncopied methods.
const RsxrHook kSessionHooks[] = {Rsxr_Mid("TU8H", &Rsxr_Mid_TU8H).Gate(kRsxrGateTuner),
                                  Rsxr_Mid("TU8C", &Rsxr_Mid_TU8C).Gate(kRsxrGateTuner),
                                  Rsxr_Mid("TU8A", &Rsxr_Mid_TU8A).Gate(kRsxrGateTuner),
                                  Rsxr_Site("TU8ALLOC").Gate(kRsxrGateTuner),
                                  Rsxr_Mid("TU8INIT", &Rsxr_Mid_TU8INIT).Gate(kRsxrGateTuner),
                                  Rsxr_Mid("TU8FREE", &Rsxr_Mid_TU8FREE, &g_rsxr_resume_TU8FREE).Gate(kRsxrGateTuner),
                                  RSXR_TU8S_SPECS(RSXR_TS8_ROW)  // TS8SEL ... TS8HNT
                                  RSXR_TS8_MID(TS8F1) RSXR_TS8_MID(TS8F2) RSXR_TS8_MID(TS8F3) RSXR_TS8_MID(TS8RS)
                                      RSXR_TS8_MID(TS8DT) RSXR_TS8_MID(TS8WR) RSXR_TS8_MID(TS8SN1) RSXR_TS8_MID(TS8SN2)
                                          RSXR_TS8_MID(TS8PA) RSXR_TS8_MID(TS8PZ)};

// The shared tuner hooks and the tuning names with their refs (group Core, as before the move).
const RsxrHook kCoreHooks[] = {
    Rsxr_Ref("X_TMGR", &g_rsxr_tuner_mgr),
    Rsxr_Ref("X_GETDET", &g_rsxr_fn_getdet),
    Rsxr_Mid("TK", &Rsxr_Mid_TK),
    Rsxr_Mid("TS", &Rsxr_Mid_TS),
    Rsxr_Ref("X_WTHR", &g_rsxr_wrong_const),
    Rsxr_Mid("W", &Rsxr_Mid_W),
    Rsxr_Mid("AT", &Rsxr_Mid_AT),
    Rsxr_WrapRow(Rsxr_Site("TJ"), kRsxrWrapTJ, &Rsxr_Mid_TJBefore, &Rsxr_Mid_TJ),
    Rsxr_WrapRow(Rsxr_Site("TU"), kRsxrWrapTU, &Rsxr_Mid_TUBefore, &Rsxr_Mid_TU),
    Rsxr_Mid("TUT", &Rsxr_Mid_TUT),
    Rsxr_WrapRow(Rsxr_Site("TN"), kRsxrWrapTN, &Rsxr_Mid_TNBefore, &Rsxr_Mid_TN),
    Rsxr_Ref("X_STRCON", &g_rsxr_fn_strcon),
    Rsxr_Ref("X_STRASG", &g_rsxr_fn_strasg),
    Rsxr_Ref("X_LOC", &g_rsxr_fn_loc),
    Rsxr_Ref("X_STRDTOR", &g_rsxr_fn_strdtor),
    Rsxr_Mid("TF1", &Rsxr_Mid_TF1, &g_rsxr_resume_TF1).Light(),
    Rsxr_Mid("TF2", &Rsxr_Mid_TF2),
};
}  // namespace

#undef RSXR_TS8_MID
#undef RSXR_TS8_ROW

extern const RsxrHookTable kRsxrTunerHooks = {Rsxr_Group(RsxrFeature::Tuner), kSessionHooks,
                                              sizeof kSessionHooks / sizeof kSessionHooks[0]};
extern const RsxrHookTable kRsxrTunerCoreHooks = {Rsxr_Group(RsxrFeature::Core), kCoreHooks,
                                                  sizeof kCoreHooks / sizeof kCoreHooks[0]};
