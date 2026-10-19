// The native N-slot chord data's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.
// Group Core, as before the move: base 7-string sites with no switch of their own; the rows with a null stub and no
// callback are in-place byte patches (the row only registers the site; the bytes come from sites.json), the X_* rows
// data references (the matcher's sort pieces and table, the tracker's eval release).
#include "features/nslot/hooks.h"
#include "features/nslot/match.h"
#include "features/nslot/nslot.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_XB1(Rs7Mid::Context&);
// hooks_records.cpp, hooks_eval.cpp, hooks_latches.cpp
void Rs7_Mid_TP1(Rs7Mid::Context&);
void Rs7_Mid_TP2(Rs7Mid::Context&);
void Rs7_Mid_TP3(Rs7Mid::Context&);
void Rs7_Mid_TP4(Rs7Mid::Context&);
void Rs7_Mid_TP5(Rs7Mid::Context&);
void Rs7_Mid_TP6(Rs7Mid::Context&);
void Rs7_Mid_TP7(Rs7Mid::Context&);
void Rs7_Mid_TP8(Rs7Mid::Context&);
void Rs7_Mid_TP12(Rs7Mid::Context&);
void Rs7_Mid_CP1(Rs7Mid::Context&);
void Rs7_Mid_CP2(Rs7Mid::Context&);
void Rs7_Mid_LU3(Rs7Mid::Context&);
void Rs7_Mid_WC2(Rs7Mid::Context&);
void Rs7_Mid_CS1(Rs7Mid::Context&);
void Rs7_Mid_CC3(Rs7Mid::Context&);
void Rs7_Mid_CH3(Rs7Mid::Context&);
void Rs7_Mid_UP1(Rs7Mid::Context&);
void Rs7_Mid_UP2(Rs7Mid::Context&);
void Rs7_Mid_UP3(Rs7Mid::Context&);
void Rs7_Mid_PD2(Rs7Mid::Context&);
void Rs7_Mid_PD3(Rs7Mid::Context&);
void Rs7_Mid_PD4(Rs7Mid::Context&);
void Rs7_Mid_PD5(Rs7Mid::Context&);
void Rs7_Mid_TP11(Rs7Mid::Context&);
void Rs7_Mid_DS1(Rs7Mid::Context&);
void Rs7_Mid_PL1(Rs7Mid::Context&);
void Rs7_Mid_SF1(Rs7Mid::Context&);  // hooks_free.cpp
void Rs7_Mid_TM(Rs7Mid::Context&);   // hooks_calls.cpp: TM1-TM6
void Rs7_Mid_EV(Rs7Mid::Context&);   // EV1-EV5
extern uintptr_t g_rs7_target_TM1, g_rs7_target_TM2, g_rs7_target_TM3, g_rs7_target_TM4, g_rs7_target_TM5,
    g_rs7_target_TM6, g_rs7_target_EV1, g_rs7_target_EV2, g_rs7_target_EV3, g_rs7_target_EV4, g_rs7_target_EV5;
extern uintptr_t g_rs7_resume_DS1;
extern uintptr_t g_rs7_resume_CC3;
extern uintptr_t g_rs7_resume_CH3;
extern uintptr_t g_rs7_fn_evrel;
void Rs7_Stub_RL1();
extern uintptr_t g_rs7_resume_RL1;
void Rs7_Stub_RL2();
extern uintptr_t g_rs7_resume_RL2;
void Rs7_Mid_AG1(Rs7Mid::Context&);  // hooks_eval.cpp: AG1-AG3
void Rs7_Mid_AG2(Rs7Mid::Context&);
void Rs7_Mid_AG3(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_UP1, g_rs7_target_UP1;
extern uintptr_t g_rs7_resume_UP2, g_rs7_target_UP2;
extern uintptr_t g_rs7_resume_UP3, g_rs7_target_UP3;
extern uintptr_t g_rs7_resume_PD2, g_rs7_target_PD2;
extern uintptr_t g_rs7_resume_PD3, g_rs7_target_PD3;
extern uintptr_t g_rs7_resume_PD4, g_rs7_target_PD4;
extern uintptr_t g_rs7_resume_PD5, g_rs7_target_PD5;
extern uintptr_t g_rs7_resume_TP12;
extern uintptr_t g_rs7_resume_PL1;
extern uintptr_t g_rs7_resume_LU3;
extern uintptr_t g_rs7_resume_WC2;
void Rs7_Mid_BT1(Rs7Mid::Context&);  // hooks_eval.cpp
extern uintptr_t g_rs7_resume_CS1;
extern uintptr_t g_rs7_resume_TP1, g_rs7_resume_TP2, g_rs7_resume_TP3, g_rs7_resume_TP4, g_rs7_resume_TP5,
    g_rs7_resume_TP6, g_rs7_resume_TP7, g_rs7_resume_TP8, g_rs7_resume_TP11, g_rs7_resume_CP1, g_rs7_resume_CP2,
    g_rs7_target_SF1;
}

namespace {
constexpr Rs7Mid::Mode kLight = Rs7Mid::Mode::Light;  // integer-only callbacks of hot sites (LIGHT_CALLBACKS)

const Rs7Hook kHooks[] = {
    {"XB1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_XB1},
    {"TP1", nullptr, &g_rs7_resume_TP1, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP1, 0, 0, 0, false, kLight},
    {"TP2", nullptr, &g_rs7_resume_TP2, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP2, 0, 0, 0, false, kLight},
    {"TP3", nullptr, &g_rs7_resume_TP3, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP3, 0, 0, 0, false, kLight},
    {"TP4", nullptr, &g_rs7_resume_TP4, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP4, 0, 0, 0, false, kLight},
    {"TP5", nullptr, &g_rs7_resume_TP5, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP5, 0, 0, 0, false, kLight},
    {"TP6", nullptr, &g_rs7_resume_TP6, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP6, 0, 0, 0, false, kLight},
    {"TP7", nullptr, &g_rs7_resume_TP7, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP7, 0, 0, 0, false, kLight},
    {"TP8", nullptr, &g_rs7_resume_TP8, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP8, 0, 0, 0, false, kLight},
    {"TP11", nullptr, &g_rs7_resume_TP11, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP11},
    {"CP1", nullptr, &g_rs7_resume_CP1, nullptr, false, false, false, 0, 0, &Rs7_Mid_CP1, 0, 0, 0, false, kLight},
    {"CP2", nullptr, &g_rs7_resume_CP2, nullptr, false, false, false, 0, 0, &Rs7_Mid_CP2, 0, 0, 0, false, kLight},
    Rs7_WrapRow({"PP1", nullptr, nullptr, nullptr, false, false}, kRs7WrapPP1, nullptr, &Rs7_Mid_PP1),
    {"SF1", nullptr, nullptr, &g_rs7_target_SF1, false, false, false, 0, 0, &Rs7_Mid_SF1},
    {"NR1", nullptr, nullptr, nullptr, false, false},
    {"NR2", nullptr, nullptr, nullptr, false, false},
    {"NR3", nullptr, nullptr, nullptr, false, false},
    {"NR4", nullptr, nullptr, nullptr, false, false},
    {"NR5", nullptr, nullptr, nullptr, false, false},
    {"NR6", nullptr, nullptr, nullptr, false, false},
    {"NB1", nullptr, nullptr, nullptr, false, false},
    {"NB2", nullptr, nullptr, nullptr, false, false},
    {"NS1", nullptr, nullptr, nullptr, false, false},
    {"NS2", nullptr, nullptr, nullptr, false, false},
    {"NS3", nullptr, nullptr, nullptr, false, false},
    {"NS4", nullptr, nullptr, nullptr, false, false},
    {"NS5", nullptr, nullptr, nullptr, false, false},
    {"NS6", nullptr, nullptr, nullptr, false, false},
    {"NS7", nullptr, nullptr, nullptr, false, false},
    {"NS8", nullptr, nullptr, nullptr, false, false},
    {"NS9", nullptr, nullptr, nullptr, false, false},
    {"NS10", nullptr, nullptr, nullptr, false, false},
    {"NS11", nullptr, nullptr, nullptr, false, false},
    {"NS12", nullptr, nullptr, nullptr, false, false},
    {"CT1", nullptr, nullptr, nullptr, false, false},
    {"CT2", nullptr, nullptr, nullptr, false, false},
    {"CT3", nullptr, nullptr, nullptr, false, false},
    {"CT4", nullptr, nullptr, nullptr, false, false},
    {"CT5", nullptr, nullptr, nullptr, false, false},
    {"CT6", nullptr, nullptr, nullptr, false, false},
    {"CT7", nullptr, nullptr, nullptr, false, false},
    {"CT8", nullptr, nullptr, nullptr, false, false},
    {"CT9", nullptr, nullptr, nullptr, false, false},
    {"CT10", nullptr, nullptr, nullptr, false, false},
    {"CT11", nullptr, nullptr, nullptr, false, false},
    {"SH1", nullptr, nullptr, nullptr, false, false},
    {"SH2", nullptr, nullptr, nullptr, false, false},
    {"SH3", nullptr, nullptr, nullptr, false, false},
    {"SH4", nullptr, nullptr, nullptr, false, false},
    {"BH1", nullptr, nullptr, nullptr, false, false},
    {"HS1", nullptr, nullptr, nullptr, false, false},
    {"HS2", nullptr, nullptr, nullptr, false, false},
    {"NN1", nullptr, nullptr, nullptr, false, false},
    {"NN2", nullptr, nullptr, nullptr, false, false},
    {"NN3", nullptr, nullptr, nullptr, false, false},
    {"NN4", nullptr, nullptr, nullptr, false, false},
    {"NN5", nullptr, nullptr, nullptr, false, false},
    {"NN6", nullptr, nullptr, nullptr, false, false},
    {"NK1", nullptr, nullptr, nullptr, false, false},
    {"TM1", nullptr, nullptr, &g_rs7_target_TM1, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"TM2", nullptr, nullptr, &g_rs7_target_TM2, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"TM3", nullptr, nullptr, &g_rs7_target_TM3, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"TM4", nullptr, nullptr, &g_rs7_target_TM4, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"TM5", nullptr, nullptr, &g_rs7_target_TM5, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"TM6", nullptr, nullptr, &g_rs7_target_TM6, false, false, false, 0, 0, &Rs7_Mid_TM, 0, 0, 0, false, kLight},
    {"NM1", nullptr, nullptr, nullptr, false, false},
    {"NM2", nullptr, nullptr, nullptr, false, false},
    {"NM3", nullptr, nullptr, nullptr, false, false},
    {"NM4", nullptr, nullptr, nullptr, false, false},
    {"NM5", nullptr, nullptr, nullptr, false, false},
    {"NM6", nullptr, nullptr, nullptr, false, false},
    {"NM7", nullptr, nullptr, nullptr, false, false},
    {"NM8", nullptr, nullptr, nullptr, false, false},
    {"NM9", nullptr, nullptr, nullptr, false, false},
    {"NM10", nullptr, nullptr, nullptr, false, false},
    {"FG1", nullptr, nullptr, nullptr, false, false},
    {"FG2", nullptr, nullptr, nullptr, false, false},
    {"FG3", nullptr, nullptr, nullptr, false, false},
    {"FG4", nullptr, nullptr, nullptr, false, false},
    {"FG6", nullptr, nullptr, nullptr, false, false},
    {"FG7", nullptr, nullptr, nullptr, false, false},
    {"FG8", nullptr, nullptr, nullptr, false, false},
    {"FG9", nullptr, nullptr, nullptr, false, false},
    {"DS1", nullptr, &g_rs7_resume_DS1, nullptr, false, false, false, 0, 0, &Rs7_Mid_DS1},
    {"SMa", nullptr, nullptr, nullptr, false, false},
    {"SMb", nullptr, nullptr, nullptr, false, false},
    {"SMc", nullptr, nullptr, nullptr, false, false},
    {"DM1", nullptr, nullptr, nullptr, false, false},
    Rs7_WrapRow({"MD1", nullptr, nullptr, nullptr, false, false}, kRs7WrapMD1, &Rs7_Mid_MD1Before, nullptr),
    {"X_MSORT", nullptr, nullptr, &g_rs7_fn_msort, false, true},
    {"X_MINS1", nullptr, nullptr, &g_rs7_fn_mins1, false, true},
    {"X_MINS2", nullptr, nullptr, &g_rs7_fn_mins2, false, true},
    {"X_MCMP", nullptr, nullptr, &g_rs7_fn_mcmp, false, true},
    {"X_MTAB", nullptr, nullptr, &g_rs7_matcher_table, false, true},
    {"RQ1", nullptr, nullptr, nullptr, false, false},
    {"RQ2", nullptr, nullptr, nullptr, false, false},
    {"RQ3", nullptr, nullptr, nullptr, false, false},
    {"RQ4", nullptr, nullptr, nullptr, false, false},
    {"RQ5", nullptr, nullptr, nullptr, false, false},
    {"RQ6", nullptr, nullptr, nullptr, false, false},
    {"PR1", nullptr, nullptr, nullptr, false, false},
    {"PR2", nullptr, nullptr, nullptr, false, false},
    {"PR3", nullptr, nullptr, nullptr, false, false},
    {"MS1", nullptr, nullptr, nullptr, false, false},
    {"MS2", nullptr, nullptr, nullptr, false, false},
    {"LU1", nullptr, nullptr, nullptr, false, false},
    {"LU2", nullptr, nullptr, nullptr, false, false},
    {"LU3", nullptr, &g_rs7_resume_LU3, nullptr, false, false, false, 0, 0, &Rs7_Mid_LU3, 0, 0, 0, false, kLight},
    Rs7_WrapRow({"AM1", nullptr, nullptr, nullptr, false, false}, kRs7WrapAM1, &Rs7_Mid_AM1Before, nullptr),
    {"WC1", nullptr, nullptr, nullptr, false, false},
    {"WC2", nullptr, &g_rs7_resume_WC2, nullptr, false, false, false, 0, 0, &Rs7_Mid_WC2, 0, 0, 0, false, kLight},
    {"WC3", nullptr, nullptr, nullptr, false, false},
    {"WC4", nullptr, nullptr, nullptr, false, false},
    {"WC5", nullptr, nullptr, nullptr, false, false},
    {"WC6", nullptr, nullptr, nullptr, false, false},
    {"WC7", nullptr, nullptr, nullptr, false, false},
    {"EV1", nullptr, nullptr, &g_rs7_target_EV1, false, false, false, 0, 0, &Rs7_Mid_EV, 0, 0, 0, false, kLight},
    {"EV2", nullptr, nullptr, &g_rs7_target_EV2, false, false, false, 0, 0, &Rs7_Mid_EV, 0, 0, 0, false, kLight},
    {"EV3", nullptr, nullptr, &g_rs7_target_EV3, false, false, false, 0, 0, &Rs7_Mid_EV, 0, 0, 0, false, kLight},
    {"EV4", nullptr, nullptr, &g_rs7_target_EV4, false, false, false, 0, 0, &Rs7_Mid_EV, 0, 0, 0, false, kLight},
    {"EV5", nullptr, nullptr, &g_rs7_target_EV5, false, false, false, 0, 0, &Rs7_Mid_EV, 0, 0, 0, false, kLight},
    {"BT1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_BT1},
    {"CS1", nullptr, &g_rs7_resume_CS1, nullptr, false, false, false, 0, 0, &Rs7_Mid_CS1, 0, 0, 0, false, kLight},
    {"TP12", nullptr, &g_rs7_resume_TP12, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP12, 0, 0, 0, false, kLight},
    {"PL1", nullptr, &g_rs7_resume_PL1, nullptr, false, false, false, 0, 0, &Rs7_Mid_PL1},
    {"CN1", nullptr, nullptr, nullptr, false, false},
    {"CN2", nullptr, nullptr, nullptr, false, false},
    {"CN3", nullptr, nullptr, nullptr, false, false},
    {"CC1", nullptr, nullptr, nullptr, false, false},
    {"CC2", nullptr, nullptr, nullptr, false, false},
    {"CC3", nullptr, &g_rs7_resume_CC3, nullptr, false, false, false, 0, 0, &Rs7_Mid_CC3, 0, 0, 0, false, kLight},
    {"BF1", nullptr, nullptr, nullptr, false, false},
    {"CH1", nullptr, nullptr, nullptr, false, false},
    {"CH2", nullptr, nullptr, nullptr, false, false},
    {"CH3", nullptr, &g_rs7_resume_CH3, nullptr, false, false, false, 0, 0, &Rs7_Mid_CH3, 0, 0, 0, false, kLight},
    {"X_EVREL", nullptr, nullptr, &g_rs7_fn_evrel, false, true},
    {"RL1", reinterpret_cast<void*>(&Rs7_Stub_RL1), &g_rs7_resume_RL1, nullptr, false, false},
    {"RL2", reinterpret_cast<void*>(&Rs7_Stub_RL2), &g_rs7_resume_RL2, nullptr, false, false},
    {"AG1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_AG1, 0, 0, 0, false, kLight},
    {"AG2", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_AG2, 0, 0, 0, false, kLight},
    {"AG3", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_AG3, 0, 0, 0, false, kLight},
    {"UP1", nullptr, &g_rs7_resume_UP1, &g_rs7_target_UP1, false, false, false, 0, 0, &Rs7_Mid_UP1, 0, 0, 0, false,
     kLight},
    {"UP2", nullptr, &g_rs7_resume_UP2, &g_rs7_target_UP2, false, false, false, 0, 0, &Rs7_Mid_UP2, 0, 0, 0, false,
     kLight},
    {"UP3", nullptr, &g_rs7_resume_UP3, &g_rs7_target_UP3, false, false, false, 0, 0, &Rs7_Mid_UP3, 0, 0, 0, false,
     kLight},
    {"PD1", nullptr, nullptr, nullptr, false, false},
    {"PD2", nullptr, &g_rs7_resume_PD2, &g_rs7_target_PD2, false, false, false, 0, 0, &Rs7_Mid_PD2, 0, 0, 0, false,
     kLight},
    {"PD3", nullptr, &g_rs7_resume_PD3, &g_rs7_target_PD3, false, false, false, 0, 0, &Rs7_Mid_PD3, 0, 0, 0, false,
     kLight},
    {"PD4", nullptr, &g_rs7_resume_PD4, &g_rs7_target_PD4, false, false, false, 0, 0, &Rs7_Mid_PD4, 0, 0, 0, false,
     kLight},
    {"PD5", nullptr, &g_rs7_resume_PD5, &g_rs7_target_PD5, false, false, false, 0, 0, &Rs7_Mid_PD5, 0, 0, 0, false,
     kLight},
    {"NF1", nullptr, nullptr, nullptr, false, false},
    {"NF2", nullptr, nullptr, nullptr, false, false},
    {"NF3", nullptr, nullptr, nullptr, false, false},
    {"NF4", nullptr, nullptr, nullptr, false, false},
};
}  // namespace

extern const Rs7HookTable kRs7NslotHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
