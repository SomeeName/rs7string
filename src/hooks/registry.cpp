#include "hooks/registry.h"
#include "core/assetload.h"
#include "core/clite.h"
#include "core/fretgrid.h"
#include "core/frets.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/neck.h"
#include "core/nextgrid.h"
#include "core/nsmatch.h"
#include "core/pitch8.h"
#include "core/publish.h"
#include "core/publish_grid.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "core/tuner.h"
#include "core/tuner8.h"
#include "core/tuner8s_ids.generated.h"
#include "core/tuningname.h"
#include "core/vguit.h"
#include "hooks/chordloops.h"
#include "hooks/mid_cold.h"
#include <cstring>

extern "C" {
#define RS7_TS8_DECL(ID, IDX) void Rs7_Stub_##ID();
RS7_TU8S_SPECS(RS7_TS8_DECL)
#undef RS7_TS8_DECL
void Rs7_Stub_NG1();
void Rs7_Stub_NGP();
void Rs7_Stub_NG9();
extern uintptr_t g_rs7_resume_NG1, g_rs7_resume_NGP, g_rs7_resume_NG9, g_rs7_target_NG9;
void Rs7_Stub_GSFR();
void Rs7_Stub_GSBD();
void Rs7_Stub_GSUL();
void Rs7_Stub_GSLD();
void Rs7_Stub_GSFO();
void Rs7_Stub_GSFI();
void Rs7_Stub_GSXF();
void Rs7_Stub_GSWD();
void Rs7_Stub_GSUP();
void Rs7_Stub_GSRS();
void Rs7_Stub_GSPK();
void Rs7_Stub_GSVB();
void Rs7_Stub_GSIC();
void Rs7_Stub_GSDM();
void Rs7_Stub_GSGL();
void Rs7_Stub_GSHC();
void Rs7_Stub_GSA1();
void Rs7_Stub_GSC1();
void Rs7_Stub_GIA1();
void Rs7_Stub_GRM1();
void Rs7_Stub_GRM2();
void Rs7_Stub_GRM3();
void Rs7_Stub_FGRB();
extern uintptr_t g_rs7_resume_FGRB;
extern uintptr_t g_rs7_resume_GSA1, g_rs7_resume_GSC1, g_rs7_target_GSC1, g_rs7_resume_GIA1, g_rs7_target_GIA1,
    g_rs7_resume_GRM1, g_rs7_resume_GRM2, g_rs7_resume_GRM3;
}
#include "core/diagrams.h"

extern "C" {
void Rs7_Stub_DGM();
void Rs7_Stub_DGS();
void Rs7_Stub_TP13();
void Rs7_Stub_DGB();
extern uintptr_t g_rs7_resume_DGB;
extern uintptr_t g_rs7_resume_DGM, g_rs7_resume_DGS, g_rs7_resume_TP13;
extern uintptr_t g_rs7_dkey, g_rs7_dnumber, g_rs7_dset, g_rs7_ddrop;
void Rs7_Stub_V2();
extern uintptr_t g_rs7_resume_V2;
void Rs7_Stub_BD1();
void Rs7_Stub_BD2();
void Rs7_Stub_BD3();
void Rs7_Stub_BD4();
extern uintptr_t g_rs7_resume_BD1, g_rs7_target_BD1, g_rs7_resume_BD2, g_rs7_target_BD2;
extern uintptr_t g_rs7_resume_BD3, g_rs7_target_BD3, g_rs7_resume_BD4, g_rs7_target_BD4;
void Rs7_Stub_S1();
void Rs7_Stub_S2();
void Rs7_Stub_S3();
void Rs7_Stub_S4();
void Rs7_Stub_I1();
extern uintptr_t g_rs7_resume_I1, g_rs7_target_I1;
void Rs7_Stub_R2();
void Rs7_Stub_R3a();
void Rs7_Stub_R3b();
void Rs7_Stub_R3c();
void Rs7_Stub_R4();
void Rs7_Stub_R6();
void Rs7_Mid_LSA(Rs7Mid::Context&);
void Rs7_Mid_LSG(Rs7Mid::Context&);
void Rs7_Mid_LSC(Rs7Mid::Context&);
void Rs7_Mid_LSE(Rs7Mid::Context&);
void Rs7_Mid_GKC(Rs7Mid::Context&);
void Rs7_Mid_GKE(Rs7Mid::Context&);
void Rs7_Mid_GKM(Rs7Mid::Context&);
void Rs7_Mid_GKL(Rs7Mid::Context&);
void Rs7_Mid_GKT(Rs7Mid::Context&);
void Rs7_Mid_GKH1(Rs7Mid::Context&);
void Rs7_Mid_GKH2(Rs7Mid::Context&);
void Rs7_Mid_FGHK(Rs7Mid::Context&);
void Rs7_Mid_FGCT(Rs7Mid::Context&);
void Rs7_Mid_D2(Rs7Mid::Context&);
void Rs7_Mid_D3(Rs7Mid::Context&);
void Rs7_Stub_D5();
void Rs7_Stub_N5();
void Rs7_Stub_N12();
void Rs7_Stub_N13();
void Rs7_Stub_N18();
void Rs7_Stub_N19();
void Rs7_Stub_N20();
void Rs7_Stub_N22();
void Rs7_Stub_N23();
void Rs7_Stub_F1();
void Rs7_Stub_LT1();
void Rs7_Stub_TF1();
void Rs7_Stub_IN1();
void Rs7_Stub_PSU1();
void Rs7_Stub_PSU2();
void Rs7_Stub_PSM1();
void Rs7_Stub_PSL2();
void Rs7_Stub_PSN2();
extern uintptr_t g_rs7_resume_PSU1, g_rs7_target_PSU1, g_rs7_resume_PSU2;
extern uintptr_t g_rs7_target_PSU2, g_rs7_resume_PSM1, g_rs7_target_PSM1;
extern uintptr_t g_rs7_target_PSL2, g_rs7_target_PSN2;
void Rs7_Stub_W();
void Rs7_Stub_AT();
void Rs7_Stub_AL1();
void Rs7_Stub_TJ();
void Rs7_Stub_TU();
void Rs7_Stub_TN();
extern uintptr_t g_rs7_resume_TJ, g_rs7_resume_TU, g_rs7_resume_TN;
void Rs7_Mid_SE1(Rs7Mid::Context&);
void Rs7_Mid_SE2(Rs7Mid::Context&);
void Rs7_Mid_SE3(Rs7Mid::Context&);
void Rs7_Stub_TP1();
void Rs7_Stub_TP2();
void Rs7_Stub_TP3();
void Rs7_Stub_TP4();
void Rs7_Stub_TP5();
void Rs7_Stub_TP6();
void Rs7_Stub_TP7();
void Rs7_Stub_TP8();
void Rs7_Stub_TP11();
void Rs7_Stub_CP1();
void Rs7_Stub_CP2();
void Rs7_Stub_PP1();
void Rs7_Stub_SF1();
void Rs7_Stub_DS1();
extern uintptr_t g_rs7_resume_DS1;
void Rs7_Stub_CC3();
extern uintptr_t g_rs7_resume_CC3;
void Rs7_Stub_CH3();
extern uintptr_t g_rs7_resume_CH3;
extern uintptr_t g_rs7_fn_evrel;
void Rs7_Stub_RL1();
extern uintptr_t g_rs7_resume_RL1;
void Rs7_Stub_RL2();
extern uintptr_t g_rs7_resume_RL2;
void Rs7_Stub_AG1();
extern uintptr_t g_rs7_resume_AG1;
void Rs7_Stub_AG2();
extern uintptr_t g_rs7_resume_AG2;
void Rs7_Stub_AG3();
extern uintptr_t g_rs7_resume_AG3;
void Rs7_Stub_UP1();
extern uintptr_t g_rs7_resume_UP1, g_rs7_target_UP1;
void Rs7_Stub_UP2();
extern uintptr_t g_rs7_resume_UP2, g_rs7_target_UP2;
void Rs7_Stub_UP3();
extern uintptr_t g_rs7_resume_UP3, g_rs7_target_UP3;
void Rs7_Stub_PD2();
extern uintptr_t g_rs7_resume_PD2, g_rs7_target_PD2;
void Rs7_Stub_PD3();
extern uintptr_t g_rs7_resume_PD3, g_rs7_target_PD3;
void Rs7_Stub_PD4();
extern uintptr_t g_rs7_resume_PD4, g_rs7_target_PD4;
void Rs7_Stub_PD5();
extern uintptr_t g_rs7_resume_PD5, g_rs7_target_PD5;
void Rs7_Stub_TP12();
extern uintptr_t g_rs7_resume_TP12;
void Rs7_Stub_PL1();
extern uintptr_t g_rs7_resume_PL1;
void Rs7_Stub_MD1();
extern uintptr_t g_rs7_resume_MD1;
void Rs7_Stub_LU3();
extern uintptr_t g_rs7_resume_LU3;
void Rs7_Stub_AM1();
extern uintptr_t g_rs7_target_AM1;
void Rs7_Stub_WC2();
extern uintptr_t g_rs7_resume_WC2;
void Rs7_Stub_EV1();
extern uintptr_t g_rs7_target_EV1;
void Rs7_Stub_EV2();
extern uintptr_t g_rs7_target_EV2;
void Rs7_Stub_EV3();
extern uintptr_t g_rs7_target_EV3;
void Rs7_Stub_EV4();
extern uintptr_t g_rs7_target_EV4;
void Rs7_Stub_EV5();
extern uintptr_t g_rs7_target_EV5;
void Rs7_Stub_BT1();
extern uintptr_t g_rs7_resume_BT1;
void Rs7_Stub_CS1();
extern uintptr_t g_rs7_resume_CS1;
void Rs7_Stub_TM1();
void Rs7_Stub_TM2();
void Rs7_Stub_TM3();
void Rs7_Stub_TM4();
void Rs7_Stub_TM5();
void Rs7_Stub_TM6();
extern uintptr_t g_rs7_target_TM1, g_rs7_target_TM2, g_rs7_target_TM3, g_rs7_target_TM4, g_rs7_target_TM5,
    g_rs7_target_TM6;
extern uintptr_t g_rs7_resume_TP1, g_rs7_resume_TP2, g_rs7_resume_TP3, g_rs7_resume_TP4, g_rs7_resume_TP5,
    g_rs7_resume_TP6, g_rs7_resume_TP7, g_rs7_resume_TP8, g_rs7_resume_TP11, g_rs7_resume_CP1, g_rs7_resume_CP2,
    g_rs7_target_PP1, g_rs7_target_SF1;
void Rs7_Stub_TICK();
void Rs7_Stub_JUD1();
void Rs7_Stub_JUD2();
void Rs7_Stub_AUD1();
void Rs7_Stub_B1();
void Rs7_Stub_B2();
extern uintptr_t g_rs7_resume_B1, g_rs7_target_B1, g_rs7_target_B2, g_rs7_fn_sst;
extern uintptr_t g_rs7_resume_TICK, g_rs7_resume_JUD1, g_rs7_resume_JUD2, g_rs7_resume_AUD1;
extern uintptr_t g_rs7_resume_AL1;
extern uintptr_t g_rs7_resume_W;
extern uintptr_t g_rs7_resume_AT;
extern uintptr_t g_rs7_resume_N5;
extern uintptr_t g_rs7_resume_N12, g_rs7_target_N12, g_rs7_resume_N13, g_rs7_target_N13;
extern uintptr_t g_rs7_resume_N18, g_rs7_target_N18, g_rs7_resume_N19, g_rs7_target_N19, g_rs7_resume_N20,
    g_rs7_target_N20;
extern uintptr_t g_rs7_resume_N22, g_rs7_target_N22, g_rs7_resume_N23, g_rs7_target_N23;
extern uintptr_t g_rs7_resume_F1, g_rs7_resume_LT1, g_rs7_resume_TF1, g_rs7_target_IN1;
extern uintptr_t g_rs7_target_S1, g_rs7_resume_S2, g_rs7_target_S2, g_rs7_target_S4;
extern uintptr_t g_rs7_resume_R2, g_rs7_resume_R3a, g_rs7_resume_R3b, g_rs7_resume_R3c;
extern uintptr_t g_rs7_resume_R4, g_rs7_resume_R6;
extern uintptr_t g_rs7_resume_D2, g_rs7_resume_D3, g_rs7_resume_D5;
}

// Stage-5 colour address ports and publication callbacks.
#include "core/colournative.h"
extern "C" {
void Rs7_Stub_PM0();
void Rs7_Stub_PR0();
void Rs7_Stub_PT1();
void Rs7_Stub_PM1();
void Rs7_Stub_PE0();
void Rs7_Stub_PC1();
void Rs7_Stub_PC2();
void Rs7_Stub_PC3();
void Rs7_Stub_PC4();
extern uintptr_t g_rs7_resume_PM0, g_rs7_resume_PR0, g_rs7_resume_PM1, g_rs7_resume_PE0;
extern uintptr_t g_rs7_target_PC1, g_rs7_target_PC2, g_rs7_target_PC3, g_rs7_target_PC4;
}

namespace {
extern "C" {
void Rs7_Stub_PUBCL();
void Rs7_Stub_PUBTO();
void Rs7_Stub_PUBWU();
void Rs7_Stub_PUBGS();
extern uintptr_t g_rs7_resume_PUBGS;
extern uintptr_t g_rs7_target_PUBGS;
void Rs7_Stub_PUBGE1();
extern uintptr_t g_rs7_resume_PUBGE1;
extern uintptr_t g_rs7_target_PUBGE1;
void Rs7_Stub_PUBGE2();
extern uintptr_t g_rs7_resume_PUBGE2;
extern uintptr_t g_rs7_target_PUBGE2;
void Rs7_Stub_PUBGC();
extern uintptr_t g_rs7_resume_PUBGC;
void Rs7_Stub_PUBGO();
extern uintptr_t g_rs7_resume_PUBGO;
void Rs7_Stub_PUBGB();
extern uintptr_t g_rs7_resume_PUBGB;
void Rs7_Stub_PUBGA();
extern uintptr_t g_rs7_resume_PUBGA;
void Rs7_Stub_PUBGF();
extern uintptr_t g_rs7_resume_PUBGF;
void Rs7_Stub_PUBEDGE();
extern uintptr_t g_rs7_resume_PUBEDGE;
void Rs7_Stub_PUBNL();
void Rs7_Stub_PUBAN();
void Rs7_Stub_PUBAC();
void Rs7_Publish_ZeroRaw();
}

// Native next-note grid site: written only with the native next-note grid.
Rs7Hook PubHook(const char* id, void* stub, uintptr_t* resume, uintptr_t* target) {
    Rs7Hook h{id, stub, resume, target, false, false};

    h.publish = true;

    return h;
}

Rs7Hook NgHook(const char* id, void* stub = nullptr, uintptr_t* resume = nullptr, uintptr_t* target = nullptr) {
    Rs7Hook h{id, stub, resume, target, false, false};

    h.ng = kRs7NgNative;

    return h;
}

extern "C" {
void Rs7_Stub_PP_TOC();
void Rs7_Stub_PP_SNGF();
bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);
extern uintptr_t g_rs7_resume_PP_TOC, g_rs7_resume_PP_SNGF, g_rs7_target_PP_SNGD;
}

const Rs7Hook kHooks[] = {
    // The plain-PSARC unit (sites.json group "psarc"): written by Rs7Plain_Start, not by the main batch.
    {"PP_SNGD", reinterpret_cast<void*>(&Rs7_Stub_PP_SNGD), nullptr, &g_rs7_target_PP_SNGD, false, false},
    {"PP_SNGF", reinterpret_cast<void*>(&Rs7_Stub_PP_SNGF), &g_rs7_resume_PP_SNGF, nullptr, false, false},
    {"PP_TOC", reinterpret_cast<void*>(&Rs7_Stub_PP_TOC), &g_rs7_resume_PP_TOC, nullptr, false, false},
#include "core/colour_hooks.generated.h"
    {"S1", reinterpret_cast<void*>(&Rs7_Stub_S1), nullptr, &g_rs7_target_S1, false, false},
    {"S2", reinterpret_cast<void*>(&Rs7_Stub_S2), &g_rs7_resume_S2, &g_rs7_target_S2, false, false},
    {"S3", reinterpret_cast<void*>(&Rs7_Stub_S3), nullptr, nullptr, false, false},
    {"I1", reinterpret_cast<void*>(&Rs7_Stub_I1), &g_rs7_resume_I1, &g_rs7_target_I1, false, false},
    {"S4", reinterpret_cast<void*>(&Rs7_Stub_S4), nullptr, &g_rs7_target_S4, false, false},
    {"R2", reinterpret_cast<void*>(&Rs7_Stub_R2), &g_rs7_resume_R2, nullptr, false, false},
    {"R3a", reinterpret_cast<void*>(&Rs7_Stub_R3a), &g_rs7_resume_R3a, nullptr, false, false},
    {"R3b", reinterpret_cast<void*>(&Rs7_Stub_R3b), &g_rs7_resume_R3b, nullptr, false, false},
    {"R3c", reinterpret_cast<void*>(&Rs7_Stub_R3c), &g_rs7_resume_R3c, nullptr, false, false},
    {"R4", reinterpret_cast<void*>(&Rs7_Stub_R4), &g_rs7_resume_R4, nullptr, false, false},
    {"R6", reinterpret_cast<void*>(&Rs7_Stub_R6), &g_rs7_resume_R6, nullptr, false, false},
    // R1: entry of Rnd_FretGrid_GetCellPos 0x58FE00; with the native ports every query is answered from the native
    // cells (PUBGP), strings 0-7, no extrapolation any more.
    PubHook("R1", reinterpret_cast<void*>(&Rs7_Stub_PUBGP), nullptr, nullptr),
    {"D2", nullptr, nullptr, nullptr, true, false, false, 0, 0, &Rs7_Mid_D2},
    {"D3", nullptr, nullptr, nullptr, true, false, false, 0, 0, &Rs7_Mid_D3},
    {"D5", reinterpret_cast<void*>(&Rs7_Stub_D5), &g_rs7_resume_D5, nullptr, true, false},
    {"X_CM", nullptr, nullptr, &g_rs7_colormgr, false, true},
    {"X_TCS", nullptr, nullptr, &g_rs7_texlist_cs, false, true},
    {"X_THEAD", nullptr, nullptr, &g_rs7_texlist_head, false, true},
    {"X_SPAWN", nullptr, nullptr, &g_rs7_fn_spawn, false, true},
    {"X_ADV3", nullptr, nullptr, &g_rs7_fn_adv3, false, true},
    {"N1", nullptr, &g_rs7_resume_N1, nullptr, false, false, false, 0, 0, &Rs7_Mid_N1},
    {"N5", reinterpret_cast<void*>(&Rs7_Stub_N5), &g_rs7_resume_N5, nullptr, false, false},
    {"X_ALLOC", nullptr, nullptr, &g_rs7_fn_alloc, false, true},
    {"X_STRCTOR", nullptr, nullptr, &g_rs7_fn_strctor, false, true},
    {"X_SHAPE", nullptr, nullptr, &g_rs7_fn_shape, false, true},
    {"X_SHOW", nullptr, nullptr, &g_rs7_fn_show, false, true},
    {"X_SETPOS", nullptr, nullptr, &g_rs7_fn_setpos, false, true},
    {"X_SETROT", nullptr, nullptr, &g_rs7_fn_setrot, false, true},
    {"X_DT", nullptr, nullptr, &g_rs7_frame_global, false, true},
    {"X_WORLD", nullptr, nullptr, &g_rs7_fn_world, false, true},
    {"X_ROOT", nullptr, nullptr, &g_rs7_fn_root, false, true},
    {"N12", reinterpret_cast<void*>(&Rs7_Stub_N12), &g_rs7_resume_N12, &g_rs7_target_N12, false, false},
    {"N13", reinterpret_cast<void*>(&Rs7_Stub_N13), &g_rs7_resume_N13, &g_rs7_target_N13, false, false},
    {"N18", reinterpret_cast<void*>(&Rs7_Stub_N18), &g_rs7_resume_N18, &g_rs7_target_N18, false, false},
    {"N19", reinterpret_cast<void*>(&Rs7_Stub_N19), &g_rs7_resume_N19, &g_rs7_target_N19, false, false},
    {"N20", reinterpret_cast<void*>(&Rs7_Stub_N20), &g_rs7_resume_N20, &g_rs7_target_N20, false, false},
    {"N21", nullptr, &g_rs7_resume_N21, nullptr, false, false, false, 0, 0, &Rs7_Mid_N21},
    {"X_DETACH", nullptr, nullptr, &g_rs7_fn_detach, false, true},
    {"N22", reinterpret_cast<void*>(&Rs7_Stub_N22), &g_rs7_resume_N22, &g_rs7_target_N22, false, false},
    {"N23", reinterpret_cast<void*>(&Rs7_Stub_N23), &g_rs7_resume_N23, &g_rs7_target_N23, false, false},
    {"X_ATTACH", nullptr, nullptr, &g_rs7_fn_attach, false, true},
#define RS7_GSE(id)                                                                                                    \
    {#id, reinterpret_cast<void*>(&Rs7_Stub_##id), nullptr, nullptr, false, false, false, 0, kRs7StrNative}
    RS7_GSE(GSFR),
    RS7_GSE(GSBD),
    RS7_GSE(GSUL),
    RS7_GSE(GSLD),
    RS7_GSE(GSFO),
    RS7_GSE(GSFI),
    RS7_GSE(GSXF),
    RS7_GSE(GSWD),
    RS7_GSE(GSUP),
    RS7_GSE(GSRS),
    RS7_GSE(GSPK),
    RS7_GSE(GSVB),
    RS7_GSE(GSIC),
    RS7_GSE(GSDM),
    RS7_GSE(GSGL),
    RS7_GSE(GSHC),
#undef RS7_GSE
#define RS7_GSN(id, res, tgt)                                                                                          \
    {#id, reinterpret_cast<void*>(&Rs7_Stub_##id), res, tgt, false, false, false, 0, kRs7StrNative}
    RS7_GSN(GSA1, &g_rs7_resume_GSA1, nullptr),
    RS7_GSN(GSC1, &g_rs7_resume_GSC1, &g_rs7_target_GSC1),
    RS7_GSN(GIA1, &g_rs7_resume_GIA1, &g_rs7_target_GIA1),
    RS7_GSN(GRM1, &g_rs7_resume_GRM1, nullptr),
    RS7_GSN(GRM2, &g_rs7_resume_GRM2, nullptr),
    RS7_GSN(GRM3, &g_rs7_resume_GRM3, nullptr),
#undef RS7_GSN
    {"GSSS", nullptr, &g_rs7_resume_GSSS, &g_rs7_target_GSSS, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSSS},
    {"GSSE", nullptr, nullptr, &g_rs7_target_GSSE, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSSE},
    {"GSZ1", nullptr, &g_rs7_resume_GSZ1, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSZ1},
    {"GIZ1", nullptr, &g_rs7_resume_GIZ1, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GIZ1},
#define RS7_FGR(id) {#id, nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, kRs7FgNative}
#include "hooks/registry_fretgrid.inc"
#undef RS7_FGR
    {"FGCT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_FGCT, kRs7FgNative},
    {"FGHK", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_FGHK, kRs7FgNative},
    {"FGRB", reinterpret_cast<void*>(&Rs7_Stub_FGRB), &g_rs7_resume_FGRB, nullptr, false, false, false, 0, 0, nullptr,
     kRs7FgNative},
#define RS7_NGB(id) NgHook(#id)
#include "hooks/registry_nextgrid.inc"
#undef RS7_NGB
    NgHook("NG1", reinterpret_cast<void*>(&Rs7_Stub_NG1), &g_rs7_resume_NG1),
    NgHook("NGP", reinterpret_cast<void*>(&Rs7_Stub_NGP), &g_rs7_resume_NGP),
    NgHook("NG9", reinterpret_cast<void*>(&Rs7_Stub_NG9), &g_rs7_resume_NG9, &g_rs7_target_NG9),
    {"X_SETSCALE", nullptr, nullptr, &g_rs7_fn_setscale, false, true},
    {"F1", reinterpret_cast<void*>(&Rs7_Stub_F1), &g_rs7_resume_F1, nullptr, false, false},
    {"LT1", reinterpret_cast<void*>(&Rs7_Stub_LT1), &g_rs7_resume_LT1, nullptr, false, false},
    {"TF1", reinterpret_cast<void*>(&Rs7_Stub_TF1), &g_rs7_resume_TF1, nullptr, false, false},
    {"IN1", reinterpret_cast<void*>(&Rs7_Stub_IN1), nullptr, &g_rs7_target_IN1, false, false},
    {"X_EULER", nullptr, nullptr, &g_rs7_fn_euler, false, true},
    {"PSA1", nullptr, nullptr, nullptr, false, false, false, kRs7PegNative},
    {"PSA2", nullptr, &g_rs7_resume_PSA2, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSA2},
    {"PSD1", nullptr, &g_rs7_resume_PSD1, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSD1},
    {"PSD2", nullptr, &g_rs7_resume_PSD2, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSD2},
    {"PSU1", reinterpret_cast<void*>(&Rs7_Stub_PSU1), &g_rs7_resume_PSU1, &g_rs7_target_PSU1, false, false, false,
     kRs7PegNative},
    {"PSU2", reinterpret_cast<void*>(&Rs7_Stub_PSU2), &g_rs7_resume_PSU2, &g_rs7_target_PSU2, false, false, false,
     kRs7PegNative},
    {"PSU3", nullptr, &g_rs7_resume_PSU3, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSU3},
    {"PSM1", reinterpret_cast<void*>(&Rs7_Stub_PSM1), &g_rs7_resume_PSM1, &g_rs7_target_PSM1, false, false, false,
     kRs7PegNative},
    {"PSM2", nullptr, &g_rs7_resume_PSM2, &g_rs7_target_PSM2, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSM2},
    {"PSQ1", nullptr, nullptr, nullptr, false, false, false, kRs7PegNative},
    {"PSC1", nullptr, nullptr, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSC1},
    {"PSL1", nullptr, &g_rs7_resume_PSL1, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSL1},
    {"PSL2", reinterpret_cast<void*>(&Rs7_Stub_PSL2), nullptr, &g_rs7_target_PSL2, false, false, false, kRs7PegNative},
    {"PSR1", nullptr, &g_rs7_resume_PSR1, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSR1},
    {"PSN1", nullptr, &g_rs7_resume_PSN1, nullptr, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSN1},
    {"X_PEGNAMES", nullptr, nullptr, &g_rs7_pegnames, false, true},
    {"PSN2", reinterpret_cast<void*>(&Rs7_Stub_PSN2), nullptr, &g_rs7_target_PSN2, false, false, false, kRs7PegNative},
    {"X_PEGSELECT", nullptr, nullptr, &g_rs7_fn_pegselect, false, true},
    {"X_PEGPLACE", nullptr, nullptr, &g_rs7_fn_pegplace, false, true},
    {"X_PEGLAYER", nullptr, nullptr, &g_rs7_fn_peglayer, false, true},
    {"X_TMGR", nullptr, nullptr, &g_rs7_tuner_mgr, false, true},
    {"X_GETDET", nullptr, nullptr, &g_rs7_fn_getdet, false, true},
    {"X_FADEMGR", nullptr, nullptr, &g_rs7_fade_mgr, false, true},
    {"X_FADEIN", nullptr, nullptr, &g_rs7_fn_fadein, false, true},
    {"X_FADEOUT", nullptr, nullptr, &g_rs7_fn_fadeout, false, true},
    {"TK", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TK},
    {"TS", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TS},
    {"W", reinterpret_cast<void*>(&Rs7_Stub_W), &g_rs7_resume_W, nullptr, false, false},
    {"AT", reinterpret_cast<void*>(&Rs7_Stub_AT), &g_rs7_resume_AT, nullptr, false, false},
    {"V2", reinterpret_cast<void*>(&Rs7_Stub_V2), &g_rs7_resume_V2, nullptr, false, false},
    {"BD1", reinterpret_cast<void*>(&Rs7_Stub_BD1), &g_rs7_resume_BD1, &g_rs7_target_BD1, false, false},
    {"BD2", reinterpret_cast<void*>(&Rs7_Stub_BD2), &g_rs7_resume_BD2, &g_rs7_target_BD2, false, false},
    {"BD3", reinterpret_cast<void*>(&Rs7_Stub_BD3), &g_rs7_resume_BD3, &g_rs7_target_BD3, false, false},
    {"BD4", reinterpret_cast<void*>(&Rs7_Stub_BD4), &g_rs7_resume_BD4, &g_rs7_target_BD4, false, false},
    {"X_FIXSTR", nullptr, nullptr, &g_rs7_fn_fixstr, false, true},
    {"AL1", reinterpret_cast<void*>(&Rs7_Stub_AL1), &g_rs7_resume_AL1, nullptr, false, false},
    {"X_STRPARAMS", nullptr, nullptr, &g_rs7_fn_strparams, false, true},
    {"X_DDSH", nullptr, nullptr, &g_rs7_fn_dds, false, true},
    {"X_NFCTOR", nullptr, nullptr, &g_rs7_fn_nfctor, false, true},
    {"X_NFDTOR", nullptr, nullptr, &g_rs7_fn_nfdtor, false, true},
    {"X_VGSLOT", nullptr, nullptr, &g_rs7_vg_slotmap, false, true},
    {"X_VGDI", nullptr, nullptr, &g_rs7_vg_dimgr, false, true},
    {"TICK", reinterpret_cast<void*>(&Rs7_Stub_TICK), &g_rs7_resume_TICK, nullptr, false, false, true},
    {"JUD1", reinterpret_cast<void*>(&Rs7_Stub_JUD1), &g_rs7_resume_JUD1, nullptr, false, false, true},
    {"JUD2", reinterpret_cast<void*>(&Rs7_Stub_JUD2), &g_rs7_resume_JUD2, nullptr, false, false, true},
    {"AUD1", reinterpret_cast<void*>(&Rs7_Stub_AUD1), &g_rs7_resume_AUD1, nullptr, false, false, true},
    {"B1", reinterpret_cast<void*>(&Rs7_Stub_B1), &g_rs7_resume_B1, &g_rs7_target_B1, false, false},
    {"X_SST", nullptr, nullptr, &g_rs7_fn_sst, false, true},
    {"B2", reinterpret_cast<void*>(&Rs7_Stub_B2), nullptr, &g_rs7_target_B2, false, false},
    {"TJ", reinterpret_cast<void*>(&Rs7_Stub_TJ), &g_rs7_resume_TJ, nullptr, false, false},
    {"TU", reinterpret_cast<void*>(&Rs7_Stub_TU), &g_rs7_resume_TU, nullptr, false, false},
    {"TUT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_TUT},
    {"TN", reinterpret_cast<void*>(&Rs7_Stub_TN), &g_rs7_resume_TN, nullptr, false, false},
    {"X_STRCON", nullptr, nullptr, &g_rs7_fn_strcon, false, true},
    {"X_STRASG", nullptr, nullptr, &g_rs7_fn_strasg, false, true},
    {"X_LOC", nullptr, nullptr, &g_rs7_fn_loc, false, true},
    {"X_STRDTOR", nullptr, nullptr, &g_rs7_fn_strdtor, false, true},
    {"SE1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_SE1},
    {"SE2", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_SE2},
    {"SE3", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_SE3},
    {"XB1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_XB1},
    {"TP1", reinterpret_cast<void*>(&Rs7_Stub_TP1), &g_rs7_resume_TP1, nullptr, false, false},
    {"TP2", reinterpret_cast<void*>(&Rs7_Stub_TP2), &g_rs7_resume_TP2, nullptr, false, false},
    {"TP3", reinterpret_cast<void*>(&Rs7_Stub_TP3), &g_rs7_resume_TP3, nullptr, false, false},
    {"TP4", reinterpret_cast<void*>(&Rs7_Stub_TP4), &g_rs7_resume_TP4, nullptr, false, false},
    {"TP5", reinterpret_cast<void*>(&Rs7_Stub_TP5), &g_rs7_resume_TP5, nullptr, false, false},
    {"TP6", reinterpret_cast<void*>(&Rs7_Stub_TP6), &g_rs7_resume_TP6, nullptr, false, false},
    {"TP7", reinterpret_cast<void*>(&Rs7_Stub_TP7), &g_rs7_resume_TP7, nullptr, false, false},
    {"TP8", reinterpret_cast<void*>(&Rs7_Stub_TP8), &g_rs7_resume_TP8, nullptr, false, false},
    {"TP11", reinterpret_cast<void*>(&Rs7_Stub_TP11), &g_rs7_resume_TP11, nullptr, false, false},
    {"CP1", reinterpret_cast<void*>(&Rs7_Stub_CP1), &g_rs7_resume_CP1, nullptr, false, false},
    {"CP2", reinterpret_cast<void*>(&Rs7_Stub_CP2), &g_rs7_resume_CP2, nullptr, false, false},
    {"PP1", reinterpret_cast<void*>(&Rs7_Stub_PP1), nullptr, &g_rs7_target_PP1, false, false},
    {"SF1", reinterpret_cast<void*>(&Rs7_Stub_SF1), nullptr, &g_rs7_target_SF1, false, false},
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
    {"TM1", reinterpret_cast<void*>(&Rs7_Stub_TM1), nullptr, &g_rs7_target_TM1, false, false},
    {"TM2", reinterpret_cast<void*>(&Rs7_Stub_TM2), nullptr, &g_rs7_target_TM2, false, false},
    {"TM3", reinterpret_cast<void*>(&Rs7_Stub_TM3), nullptr, &g_rs7_target_TM3, false, false},
    {"TM4", reinterpret_cast<void*>(&Rs7_Stub_TM4), nullptr, &g_rs7_target_TM4, false, false},
    {"TM5", reinterpret_cast<void*>(&Rs7_Stub_TM5), nullptr, &g_rs7_target_TM5, false, false},
    {"TM6", reinterpret_cast<void*>(&Rs7_Stub_TM6), nullptr, &g_rs7_target_TM6, false, false},
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
    {"DS1", reinterpret_cast<void*>(&Rs7_Stub_DS1), &g_rs7_resume_DS1, nullptr, false, false},
    {"SMa", nullptr, nullptr, nullptr, false, false},
    {"SMb", nullptr, nullptr, nullptr, false, false},
    {"SMc", nullptr, nullptr, nullptr, false, false},
    {"DM1", nullptr, nullptr, nullptr, false, false},
    {"MD1", reinterpret_cast<void*>(&Rs7_Stub_MD1), &g_rs7_resume_MD1, nullptr, false, false},
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
    {"LU3", reinterpret_cast<void*>(&Rs7_Stub_LU3), &g_rs7_resume_LU3, nullptr, false, false},
    {"AM1", reinterpret_cast<void*>(&Rs7_Stub_AM1), nullptr, &g_rs7_target_AM1, false, false},
    {"WC1", nullptr, nullptr, nullptr, false, false},
    {"WC2", reinterpret_cast<void*>(&Rs7_Stub_WC2), &g_rs7_resume_WC2, nullptr, false, false},
    {"WC3", nullptr, nullptr, nullptr, false, false},
    {"WC4", nullptr, nullptr, nullptr, false, false},
    {"WC5", nullptr, nullptr, nullptr, false, false},
    {"WC6", nullptr, nullptr, nullptr, false, false},
    {"WC7", nullptr, nullptr, nullptr, false, false},
    {"EV1", reinterpret_cast<void*>(&Rs7_Stub_EV1), nullptr, &g_rs7_target_EV1, false, false},
    {"EV2", reinterpret_cast<void*>(&Rs7_Stub_EV2), nullptr, &g_rs7_target_EV2, false, false},
    {"EV3", reinterpret_cast<void*>(&Rs7_Stub_EV3), nullptr, &g_rs7_target_EV3, false, false},
    {"EV4", reinterpret_cast<void*>(&Rs7_Stub_EV4), nullptr, &g_rs7_target_EV4, false, false},
    {"EV5", reinterpret_cast<void*>(&Rs7_Stub_EV5), nullptr, &g_rs7_target_EV5, false, false},
    {"BT1", reinterpret_cast<void*>(&Rs7_Stub_BT1), &g_rs7_resume_BT1, nullptr, false, false},
    {"CS1", reinterpret_cast<void*>(&Rs7_Stub_CS1), &g_rs7_resume_CS1, nullptr, false, false},
    {"TP12", reinterpret_cast<void*>(&Rs7_Stub_TP12), &g_rs7_resume_TP12, nullptr, false, false},
    {"PL1", reinterpret_cast<void*>(&Rs7_Stub_PL1), &g_rs7_resume_PL1, nullptr, false, false},
    {"CN1", nullptr, nullptr, nullptr, false, false},
    {"CN2", nullptr, nullptr, nullptr, false, false},
    {"CN3", nullptr, nullptr, nullptr, false, false},
    {"CC1", nullptr, nullptr, nullptr, false, false},
    {"CC2", nullptr, nullptr, nullptr, false, false},
    {"CC3", reinterpret_cast<void*>(&Rs7_Stub_CC3), &g_rs7_resume_CC3, nullptr, false, false},
    {"BF1", nullptr, nullptr, nullptr, false, false},
    {"CH1", nullptr, nullptr, nullptr, false, false},
    {"CH2", nullptr, nullptr, nullptr, false, false},
    {"CH3", reinterpret_cast<void*>(&Rs7_Stub_CH3), &g_rs7_resume_CH3, nullptr, false, false},
    {"X_EVREL", nullptr, nullptr, &g_rs7_fn_evrel, false, true},
    {"RL1", reinterpret_cast<void*>(&Rs7_Stub_RL1), &g_rs7_resume_RL1, nullptr, false, false},
    {"RL2", reinterpret_cast<void*>(&Rs7_Stub_RL2), &g_rs7_resume_RL2, nullptr, false, false},
    {"AG1", reinterpret_cast<void*>(&Rs7_Stub_AG1), &g_rs7_resume_AG1, nullptr, false, false},
    {"AG2", reinterpret_cast<void*>(&Rs7_Stub_AG2), &g_rs7_resume_AG2, nullptr, false, false},
    {"AG3", reinterpret_cast<void*>(&Rs7_Stub_AG3), &g_rs7_resume_AG3, nullptr, false, false},
    {"UP1", reinterpret_cast<void*>(&Rs7_Stub_UP1), &g_rs7_resume_UP1, &g_rs7_target_UP1, false, false},
    {"UP2", reinterpret_cast<void*>(&Rs7_Stub_UP2), &g_rs7_resume_UP2, &g_rs7_target_UP2, false, false},
    {"UP3", reinterpret_cast<void*>(&Rs7_Stub_UP3), &g_rs7_resume_UP3, &g_rs7_target_UP3, false, false},
    {"PD1", nullptr, nullptr, nullptr, false, false},
    {"PD2", reinterpret_cast<void*>(&Rs7_Stub_PD2), &g_rs7_resume_PD2, &g_rs7_target_PD2, false, false},
    {"PD3", reinterpret_cast<void*>(&Rs7_Stub_PD3), &g_rs7_resume_PD3, &g_rs7_target_PD3, false, false},
    {"PD4", reinterpret_cast<void*>(&Rs7_Stub_PD4), &g_rs7_resume_PD4, &g_rs7_target_PD4, false, false},
    {"PD5", reinterpret_cast<void*>(&Rs7_Stub_PD5), &g_rs7_resume_PD5, &g_rs7_target_PD5, false, false},
    {"NF1", nullptr, nullptr, nullptr, false, false},
    {"NF2", nullptr, nullptr, nullptr, false, false},
    {"NF3", nullptr, nullptr, nullptr, false, false},
    {"NF4", nullptr, nullptr, nullptr, false, false},
    {"DGM", reinterpret_cast<void*>(&Rs7_Stub_DGM), &g_rs7_resume_DGM, nullptr, false, false},
    {"DGB", reinterpret_cast<void*>(&Rs7_Stub_DGB), &g_rs7_resume_DGB, nullptr, false, false},
    {"DGS", reinterpret_cast<void*>(&Rs7_Stub_DGS), &g_rs7_resume_DGS, nullptr, false, false},
    {"TP13", reinterpret_cast<void*>(&Rs7_Stub_TP13), &g_rs7_resume_TP13, nullptr, false, false},
    {"X_DKEY", nullptr, nullptr, &g_rs7_dkey, false, true},
    {"X_DNUMBER", nullptr, nullptr, &g_rs7_dnumber, false, true},
    {"X_DSET", nullptr, nullptr, &g_rs7_dset, false, true},
    {"X_DDROP", nullptr, nullptr, &g_rs7_ddrop, false, true},
// [neck] native_pitch: native 8-entry tuning / open-MIDI storage (core/pitch8.h, issue #18 stage 5 area 3).
#define RS7_PITCH(id, stub, res, tgt) {id, stub, res, tgt, false, false, false, 0, 0, nullptr, 0, kRs7PitchNative}
    RS7_PITCH("PA1", nullptr, nullptr, nullptr),
    RS7_PITCH("PA2", nullptr, nullptr, nullptr),
    RS7_PITCH("PM0", reinterpret_cast<void*>(&Rs7_Stub_PM0), &g_rs7_resume_PM0, nullptr),
    RS7_PITCH("PR0", reinterpret_cast<void*>(&Rs7_Stub_PR0), &g_rs7_resume_PR0, nullptr),
    RS7_PITCH("PT1", reinterpret_cast<void*>(&Rs7_Stub_PT1), nullptr, nullptr),
    RS7_PITCH("PM1", reinterpret_cast<void*>(&Rs7_Stub_PM1), &g_rs7_resume_PM1, nullptr),
    RS7_PITCH("PE0", reinterpret_cast<void*>(&Rs7_Stub_PE0), &g_rs7_resume_PE0, nullptr),
    RS7_PITCH("PC1", reinterpret_cast<void*>(&Rs7_Stub_PC1), nullptr, &g_rs7_target_PC1),
    RS7_PITCH("PC2", reinterpret_cast<void*>(&Rs7_Stub_PC2), nullptr, &g_rs7_target_PC2),
    RS7_PITCH("PC3", reinterpret_cast<void*>(&Rs7_Stub_PC3), nullptr, &g_rs7_target_PC3),
    RS7_PITCH("PC4", reinterpret_cast<void*>(&Rs7_Stub_PC4), nullptr, &g_rs7_target_PC4),
#undef RS7_PITCH
    {"PUBCL", reinterpret_cast<void*>(&Rs7_Stub_PUBCL), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     true},
    {"PUBTO", reinterpret_cast<void*>(&Rs7_Stub_PUBTO), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     true},
    {"PUBWU", reinterpret_cast<void*>(&Rs7_Stub_PUBWU), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     true},
    {"PUBNN1", nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0, true},
    {"PUBNN3", nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0, true},
    {"PUBNN4", nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0, true},
    PubHook("PUBGS", reinterpret_cast<void*>(&Rs7_Stub_PUBGS), &g_rs7_resume_PUBGS, &g_rs7_target_PUBGS),
    PubHook("PUBGE1", reinterpret_cast<void*>(&Rs7_Stub_PUBGE1), &g_rs7_resume_PUBGE1, &g_rs7_target_PUBGE1),
    PubHook("PUBGE2", reinterpret_cast<void*>(&Rs7_Stub_PUBGE2), &g_rs7_resume_PUBGE2, &g_rs7_target_PUBGE2),
    PubHook("PUBGC", reinterpret_cast<void*>(&Rs7_Stub_PUBGC), &g_rs7_resume_PUBGC, nullptr),
    PubHook("PUBGO", reinterpret_cast<void*>(&Rs7_Stub_PUBGO), &g_rs7_resume_PUBGO, nullptr),
    PubHook("PUBGB", reinterpret_cast<void*>(&Rs7_Stub_PUBGB), &g_rs7_resume_PUBGB, nullptr),
    PubHook("PUBGA", reinterpret_cast<void*>(&Rs7_Stub_PUBGA), &g_rs7_resume_PUBGA, nullptr),
    PubHook("PUBGF", reinterpret_cast<void*>(&Rs7_Stub_PUBGF), &g_rs7_resume_PUBGF, nullptr),
    PubHook("PUBEDGE", reinterpret_cast<void*>(&Rs7_Stub_PUBEDGE), &g_rs7_resume_PUBEDGE, nullptr),
    PubHook("PUBNL", reinterpret_cast<void*>(&Rs7_Stub_PUBNL), nullptr, nullptr),
    PubHook("PUBAN", reinterpret_cast<void*>(&Rs7_Stub_PUBAN), nullptr, nullptr),
    PubHook("PUBAC", reinterpret_cast<void*>(&Rs7_Stub_PUBAC), nullptr, nullptr),
    PubHook("PUBZ0", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ1", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ2", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ3", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ4", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ5", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
    PubHook("PUBZ6", reinterpret_cast<void*>(&Rs7_Publish_ZeroRaw), nullptr, nullptr),
#include "hooks/registry_chordloops.inc"
    {"TU8H", reinterpret_cast<void*>(&Rs7_Stub_TU8H), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     false, kRs7TunerNative},
    {"TU8C", reinterpret_cast<void*>(&Rs7_Stub_TU8C), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     false, kRs7TunerNative},
    {"TU8A", reinterpret_cast<void*>(&Rs7_Stub_TU8A), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0,
     false, kRs7TunerNative},
    {"TU8ALLOC", nullptr, nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0, 0, false, kRs7TunerNative},
    {"TU8INIT", reinterpret_cast<void*>(&Rs7_Stub_TU8INIT), nullptr, nullptr, false, false, false, 0, 0, nullptr, 0, 0,
     0, false, kRs7TunerNative},
    {"TU8FREE", reinterpret_cast<void*>(&Rs7_Stub_TU8FREE), &g_rs7_resume_TU8FREE, nullptr, false, false, false, 0, 0,
     nullptr, 0, 0, 0, false, kRs7TunerNative},
// Native tuner session copies (tools/gen_tuner8s.py) and mid-hooks (core/tuner8.h).
#define RS7_TS8(ID, IDX)                                                                                               \
    {#ID,                                                                                                              \
     reinterpret_cast<void*>(&Rs7_Stub_##ID),                                                                          \
     nullptr,                                                                                                          \
     nullptr,                                                                                                          \
     false,                                                                                                            \
     false,                                                                                                            \
     false,                                                                                                            \
     0,                                                                                                                \
     0,                                                                                                                \
     nullptr,                                                                                                          \
     0,                                                                                                                \
     0,                                                                                                                \
     0,                                                                                                                \
     false,                                                                                                            \
     kRs7TunerNative},
    RS7_TU8S_SPECS(RS7_TS8)
#undef RS7_TS8
#define RS7_TS8MID(ID)                                                                                                 \
    {#ID, nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_##ID, 0, 0, 0, false, kRs7TunerNative},
        RS7_TS8MID(TS8F1) RS7_TS8MID(TS8F2) RS7_TS8MID(TS8F3) RS7_TS8MID(TS8RS) RS7_TS8MID(TS8DT) RS7_TS8MID(TS8WR)
            RS7_TS8MID(TS8SN1) RS7_TS8MID(TS8SN2) RS7_TS8MID(TS8PA) RS7_TS8MID(TS8PZ)
#undef RS7_TS8MID
    // Rnd_Guitar_LoadStep probes (core/loadprobe.cpp), diagnostic: written only with [debug] publish_trace=1.
    {"LSA", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSA},
    {"LSG", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSG},
    {"LSC", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSC},
    {"LSE", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSE},
    // C-lite (core/clite.h, prototype): the string count as a property of the guitar build.
    {"GKC", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKC},
    {"GKE", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKE},
    {"GKM", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKM},
    {"GKL", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKL},
    {"GKT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKT},
    {"GKH1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKH1},
    {"GKH2", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKH2},
};
}  // namespace

const Rs7Hook* Rs7_FindHook(const char* id) {
    for (const auto& h : kHooks) {
        if (std::strcmp(h.id, id) == 0) {
            return &h;
        }
    }

    return nullptr;
}

namespace {
bool g_featureOff[size_t(Rs7Feature::Count)] = {};
}

Rs7Feature Rs7_FeatureOf(const Rs7Hook& h) {
    if (h.ref) {
        return Rs7Feature::Core;  // data references are shared with the core
    }
    if (!std::strncmp(h.id, "PP_", 3)) {
        return Rs7Feature::Psarc;  // the plain-PSARC unit (core/plainpsarc.h)
    }
    if (!std::strncmp(h.id, "GK", 2)) {
        return Rs7Feature::Clite;  // C-lite (core/clite.h)
    }
    if (h.publish) {
        return Rs7Feature::Publish;  // every PUB* site, whatever its layout fields
    }
    if (h.tuner) {
        return Rs7Feature::Tuner;  // TU8* / TS8* (native_tuner)
    }
    if (!std::strncmp(h.id, "CL", 2)) {
        return Rs7Feature::Chords;  // chordloops (native_chordloops)
    }
    if (!std::strncmp(h.id, "CM", 2)) {
        return Rs7Feature::Colours;
    }
    if (!std::strncmp(h.id, "DG", 2) || !std::strcmp(h.id, "TP13")) {
        return Rs7Feature::Diagrams;
    }
    if (h.pegs) {
        return Rs7Feature::Pegs;
    }
    if (h.strs) {
        return Rs7Feature::Strings;
    }
    if (h.fg) {
        return Rs7Feature::Fretgrid;
    }
    if (h.pitch) {
        return Rs7Feature::Pitch;
    }
    if (h.ng) {
        return Rs7Feature::Nextgrid;
    }
    if (h.vgOnly) {
        return Rs7Feature::Vguitar;
    }
    if (h.diagOnly) {
        return Rs7Feature::Diag;
    }

    return Rs7Feature::Core;
}

const char* Rs7_FeatureName(Rs7Feature f) {
    static const char* const n[] = {"core",
                                    "native colours",
                                    "native PegSet",
                                    "native GuitarStrings",
                                    "native FretGrid",
                                    "native pitch storage",
                                    "native next-note grid",
                                    "[diagrams]",
                                    "[test] vguitar",
                                    "[debug] diagnostics",
                                    "native publication ports",
                                    "[chords] native_chordloops",
                                    "native tuner",
                                    "[psarc] plain",
                                    "C-lite"};

    return size_t(f) < size_t(Rs7Feature::Count) ? n[size_t(f)] : "?";
}

bool Rs7_IsSevenUnit(Rs7Feature f) {
    switch (f) {
        case Rs7Feature::Colours:
        case Rs7Feature::Pegs:
        case Rs7Feature::Strings:
        case Rs7Feature::Fretgrid:
        case Rs7Feature::Pitch:
        case Rs7Feature::Nextgrid:
        case Rs7Feature::Publish:
        case Rs7Feature::Tuner:
        case Rs7Feature::Clite:
            return true;
        default:
            return false;
    }
}

bool Rs7_FeatureDisabled(Rs7Feature f) {
    return size_t(f) < size_t(Rs7Feature::Count) && g_featureOff[size_t(f)];
}

void Rs7_ResetFeatureFailures() {
    for (auto& b : g_featureOff) {
        b = false;
    }
}

bool Rs7_DisableFeature(Rs7Feature f) {
    if (f == Rs7Feature::Core || size_t(f) >= size_t(Rs7Feature::Count)) {
        return false;
    }

    g_featureOff[size_t(f)] = true;

    switch (f) {  // runtime switches read by stubs / callbacks
        case Rs7Feature::Colours:
            g_rs7_native_colours = 0;
            break;
        case Rs7Feature::Pegs:
            g_rs7_native_pegset = 0;
            break;
        case Rs7Feature::Strings:
            g_rs7_native_strings = 0;
            break;
        case Rs7Feature::Fretgrid:
            g_rs7_native_fretgrid = 0;
            break;
        case Rs7Feature::Pitch:
            g_rs7_native_pitch = 0;
            break;
        case Rs7Feature::Nextgrid:
            g_rs7_native_nextgrid = 0;
            break;
        case Rs7Feature::Diagrams:
            g_rs7_diagrams = 0;
            break;
        case Rs7Feature::Publish:
            g_rs7_publish_ports = 0;
            g_rs7_native_publish = 0;
            break;
        case Rs7Feature::Chords:
            g_rs7_native_chordloops = 0;
            break;
        case Rs7Feature::Tuner:
            g_rs7_native_tuner = 0;
            break;
        case Rs7Feature::Clite:
            g_rs7_clite = 0;
            break;
        default:
            break;
    }

    // Native 7-string mode is both halves: C-lite (the guitar is built with the count) and the native_publish
    // ports (the layouts that can show it). One half failing takes the other with it: a half cannot stand alone.
    if (f == Rs7Feature::Clite && g_rs7_publish_ports) {
        g_featureOff[size_t(Rs7Feature::Publish)] = true;
        g_rs7_publish_ports = g_rs7_native_publish = 0;
    }

    if (f == Rs7Feature::Publish && g_rs7_clite) {
        g_featureOff[size_t(Rs7Feature::Clite)] = true;
        g_rs7_clite = 0;
    }

    // The native tuner session carries copies of the GuitarStrings / FretGrid / colour edits: without any of them it
    // must go too.
    if (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours) {
        g_rs7_native_tuner = 0;
    }

    // A guitar built with 7 needs the native PegSet (review F1): GKC writes peg request 7/8 and the stock
    // Rnd_PegSet_Load 0x59C4D0 would loop over it on the six-slot stock layout. Any loss of Pegs (startup config or a
    // runtime isolation after the initial configuration) takes clite and the ports with it before the next round.
    if (!g_rs7_native_pegset && (g_rs7_clite || g_rs7_publish_ports)) {
        g_featureOff[size_t(Rs7Feature::Clite)] = true;
        g_rs7_clite = 0;
        g_featureOff[size_t(Rs7Feature::Publish)] = true;
        g_rs7_publish_ports = g_rs7_native_publish = 0;
    }

    // A guitar built with 7 needs the native 8-slot tuner session (fix/clite3, docs/ARCHITECTURE.md): the stock
    // session's per-string arrays have six slots and its loops run to info+0Bh. clite goes, and native_publish with it.
    if (!g_rs7_native_tuner && g_rs7_clite) {
        g_featureOff[size_t(Rs7Feature::Clite)] = true;
        g_rs7_clite = 0;

        if (g_rs7_publish_ports) {
            g_featureOff[size_t(Rs7Feature::Publish)] = true;
            g_rs7_publish_ports = g_rs7_native_publish = 0;
        }
    }

    // native_publish needs all five native layouts (core/publish.h PortsEnabled); chordloops needs native_strings.
    if (g_rs7_publish_ports && (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours ||
                                !g_rs7_native_pitch || !g_rs7_native_nextgrid)) {
        g_featureOff[size_t(Rs7Feature::Publish)] = true;
        g_rs7_publish_ports = g_rs7_native_publish = 0;

        if (g_rs7_clite) {  // ... and without the ports a guitar built with 7 strings has nothing to show it
            g_featureOff[size_t(Rs7Feature::Clite)] = true;
            g_rs7_clite = 0;
        }
    }

    if (g_rs7_native_chordloops && !g_rs7_native_strings) {
        g_featureOff[size_t(Rs7Feature::Chords)] = true;
        g_rs7_native_chordloops = 0;
    }

    return true;
}

bool Rs7_ShouldPatch(const Rs7Hook& h, bool diagnostics, bool vguitar, bool nativePegs, bool nativeStrings) {
    if (g_featureOff[size_t(Rs7_FeatureOf(h))]) {
        return false;
    }
    if (h.tuner == kRs7TunerNative &&
        (!g_rs7_native_tuner || !nativeStrings || !g_rs7_native_fretgrid || !g_rs7_native_colours)) {
        return false;
    }
    if (h.publish && !g_rs7_publish_ports) {
        return false;
    }
    if (h.id[0] == 'G' && h.id[1] == 'K' && !g_rs7_clite) {
        return false;  // C-lite
    }
    if (g_rs7_clite && !std::strcmp(h.id, "PSQ1")) {
        return false;  // GKC writes the peg word (core/clite.h)
    }
    if (h.id[0] == 'L' && h.id[1] == 'S' && h.id[2] && !h.id[3] && !g_rs7_publish_trace) {
        return false;  // LSA/LSG/LSC/LSE probes
    }
    if (g_rs7_publish_ports && !std::strcmp(h.id, "FGRB")) {
        return false;
    }
    if (g_rs7_publish_ports && (!std::strcmp(h.id, "NN1") || !std::strcmp(h.id, "NN3") || !std::strcmp(h.id, "NN4"))) {
        return false;
    }

    const bool chordloops = g_rs7_native_chordloops && nativeStrings;
    if (!std::strncmp(h.id, "CL", 2) && !chordloops) {
        return false;
    }
    if (chordloops && Rs7ChordLoops_Replaced(h.id)) {
        return false;
    }
    if (!std::strncmp(h.id, "CM", 2) && !g_rs7_native_colours) {
        return false;
    }
    if ((!std::strncmp(h.id, "DG", 2) || !std::strcmp(h.id, "TP13")) && !g_rs7_diagrams) {
        return false;
    }
    if (h.pegs == kRs7PegNative && !nativePegs) {
        return false;
    }
    if (h.strs == kRs7StrNative && !nativeStrings) {
        return false;
    }
    if (h.fg == kRs7FgNative && !g_rs7_native_fretgrid) {
        return false;
    }
    if (h.pitch == kRs7PitchNative && !g_rs7_native_pitch) {
        return false;
    }
    if (h.ng == kRs7NgNative && !g_rs7_native_nextgrid) {
        return false;
    }

    return !h.ref && (diagnostics || !h.diagOnly) && (vguitar || !h.vgOnly);
}
