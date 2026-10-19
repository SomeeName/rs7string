#include "features/tuner/hooks.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8s_ids.generated.h"
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
extern uintptr_t g_rs7_resume_TF1;
}

// A native-session row: written only with the native tuner and its prerequisites (Rs7_ShouldPatch).
// clang-format off
#define RS7_TU8_ROW(ID, STUB, RESUME, CALLBACK)                                    \
    {ID, STUB, RESUME, nullptr, false, false, false, 0, 0, CALLBACK, 0, 0, 0, false, \
     Rs7Mid::Mode::Full, kRs7TunerNative}
// clang-format on
#define RS7_TS8_ROW(ID, IDX) RS7_TU8_ROW(#ID, nullptr, nullptr, &Rs7_Mid_##ID),
#define RS7_TS8_MID(ID) RS7_TU8_ROW(#ID, nullptr, nullptr, &Rs7_Mid_##ID),

namespace {
// The native 8-slot tuner session (group Tuner): GuitarStrings readers, the grown session, the method copies
// (tools/gen_tuner8s.py) and the mid-hooks in the uncopied methods.
const Rs7Hook kSessionHooks[] = {RS7_TU8_ROW("TU8H", nullptr, nullptr, &Rs7_Mid_TU8H),
                                 RS7_TU8_ROW("TU8C", nullptr, nullptr, &Rs7_Mid_TU8C),
                                 RS7_TU8_ROW("TU8A", nullptr, nullptr, &Rs7_Mid_TU8A),
                                 RS7_TU8_ROW("TU8ALLOC", nullptr, nullptr, nullptr),
                                 RS7_TU8_ROW("TU8INIT", nullptr, nullptr, &Rs7_Mid_TU8INIT),
                                 RS7_TU8_ROW("TU8FREE", nullptr, &g_rs7_resume_TU8FREE, &Rs7_Mid_TU8FREE),
                                 RS7_TU8S_SPECS(RS7_TS8_ROW)  // TS8SEL ... TS8HNT
                                 RS7_TS8_MID(TS8F1) RS7_TS8_MID(TS8F2) RS7_TS8_MID(TS8F3) RS7_TS8_MID(TS8RS)
                                     RS7_TS8_MID(TS8DT) RS7_TS8_MID(TS8WR) RS7_TS8_MID(TS8SN1) RS7_TS8_MID(TS8SN2)
                                         RS7_TS8_MID(TS8PA) RS7_TS8_MID(TS8PZ)};

// The shared tuner hooks and the tuning names with their refs (group Core, as before the move).
const Rs7Hook kCoreHooks[] = {
    {"X_TMGR", nullptr, nullptr, &g_rs7_tuner_mgr, false, true},
    {"X_GETDET", nullptr, nullptr, &g_rs7_fn_getdet, false, true},
    {"TK", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TK},
    {"TS", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TS},
    {"X_WTHR", nullptr, nullptr, &g_rs7_wrong_const, false, true},
    {"W", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_W},
    {"AT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_AT},
    Rs7_WrapRow({"TJ", nullptr, nullptr, nullptr, false, false}, kRs7WrapTJ, &Rs7_Mid_TJBefore, &Rs7_Mid_TJ),
    Rs7_WrapRow({"TU", nullptr, nullptr, nullptr, false, false}, kRs7WrapTU, &Rs7_Mid_TUBefore, &Rs7_Mid_TU),
    {"TUT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TUT},
    Rs7_WrapRow({"TN", nullptr, nullptr, nullptr, false, false}, kRs7WrapTN, &Rs7_Mid_TNBefore, &Rs7_Mid_TN),
    {"X_STRCON", nullptr, nullptr, &g_rs7_fn_strcon, false, true},
    {"X_STRASG", nullptr, nullptr, &g_rs7_fn_strasg, false, true},
    {"X_LOC", nullptr, nullptr, &g_rs7_fn_loc, false, true},
    {"X_STRDTOR", nullptr, nullptr, &g_rs7_fn_strdtor, false, true},
    {"TF1", nullptr, &g_rs7_resume_TF1, nullptr, false, false, false, 0, 0, &Rs7_Mid_TF1, 0, 0, 0, false,
     Rs7Mid::Mode::Light},
};
}  // namespace

#undef RS7_TS8_MID
#undef RS7_TS8_ROW
#undef RS7_TU8_ROW

extern const Rs7HookTable kRs7TunerHooks = {Rs7_Group(Rs7Feature::Tuner), kSessionHooks,
                                            sizeof kSessionHooks / sizeof kSessionHooks[0]};
extern const Rs7HookTable kRs7TunerCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                sizeof kCoreHooks / sizeof kCoreHooks[0]};
