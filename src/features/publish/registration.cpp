// The publication's hooks (group Publish), looked up by src/registry.cpp (Rs7_FindHook) and written by the main
// batch, only with the publication ports (Rs7_ShouldPatch: the rows' `publish` flag). PUBNN1 PUBNN3 PUBNN4 have a
// null stub: in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/publish/hooks.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_PUBGP(Rs7Mid::Context&);  // hooks_gridsites.cpp (site R1, sse mode)
#define PUB_DECL(id) void Rs7_Mid_##id(Rs7Mid::Context&);
#define PUB_DECL_RESUME(id) PUB_DECL(id) extern uintptr_t g_rs7_resume_##id;
#define PUB_DECL_GUARD(id) PUB_DECL_RESUME(id) extern uintptr_t g_rs7_target_##id;
// clang-format off
PUB_DECL(PUBCL) PUB_DECL(PUBTO) PUB_DECL(PUBWU) PUB_DECL(PUBNL) PUB_DECL(PUBAN) PUB_DECL(PUBAC)  // hooks_entries.cpp
PUB_DECL_GUARD(PUBGS) PUB_DECL_GUARD(PUBGE1) PUB_DECL_GUARD(PUBGE2)  // hooks_gridsites.cpp
PUB_DECL_RESUME(PUBGC) PUB_DECL_RESUME(PUBGO) PUB_DECL_RESUME(PUBGB) PUB_DECL(PUBGA) PUB_DECL(PUBGF)
PUB_DECL_RESUME(PUBEDGE)
PUB_DECL(PUBZ)  // hooks_pitch.cpp: PUBZ0-PUBZ6, the callees of the call sites
extern uintptr_t g_rs7_target_PUBZ0, g_rs7_target_PUBZ4, g_rs7_target_PUBZ5, g_rs7_target_PUBZ6;
// clang-format on
#undef PUB_DECL_GUARD
#undef PUB_DECL_RESUME
#undef PUB_DECL
}

namespace {
#define RS7_PUB_ROW(ID, STUB, RESUME, TARGET)                                                                          \
    {ID, STUB, RESUME, TARGET, false, false, false, 0, 0, nullptr, 0, 0, 0, true}
// A mid-hook row (light: per note / cell, integer only; full: the rest).
#define RS7_PUB_MID(ID, RESUME, TARGET, MODE)                                                                          \
    {#ID, nullptr, RESUME, TARGET, false, false, false, 0, 0, &Rs7_Mid_##ID, 0, 0, 0, true, Rs7Mid::Mode::MODE}
// PUBZ0-PUBZ6: one light callback (call sites before the callee, jmp sites with the relocated jmp as the replay).
#define RS7_PUB_ZERO(ID, TARGET)                                                                                       \
    {ID, nullptr, nullptr, TARGET, false, false, false, 0, 0, &Rs7_Mid_PUBZ, 0, 0, 0, true, Rs7Mid::Mode::Light}

const Rs7Hook kHooks[] = {
    // R1: entry of Rnd_FretGrid_GetCellPos 0x58FE00; with the native ports every query is answered from the native
    // cells (PUBGP), strings 0-7, no extrapolation any more.
    {"R1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_PUBGP, 0, 0, 0, true, Rs7Mid::Mode::Sse},
    RS7_PUB_MID(PUBCL, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBTO, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBWU, nullptr, nullptr, Light),
    RS7_PUB_ROW("PUBNN1", nullptr, nullptr, nullptr),
    RS7_PUB_ROW("PUBNN3", nullptr, nullptr, nullptr),
    RS7_PUB_ROW("PUBNN4", nullptr, nullptr, nullptr),
    RS7_PUB_MID(PUBGS, &g_rs7_resume_PUBGS, &g_rs7_target_PUBGS, Light),
    RS7_PUB_MID(PUBGE1, &g_rs7_resume_PUBGE1, &g_rs7_target_PUBGE1, Light),
    RS7_PUB_MID(PUBGE2, &g_rs7_resume_PUBGE2, &g_rs7_target_PUBGE2, Light),
    RS7_PUB_MID(PUBGC, &g_rs7_resume_PUBGC, nullptr, Light),
    RS7_PUB_MID(PUBGO, &g_rs7_resume_PUBGO, nullptr, Light),
    RS7_PUB_MID(PUBGB, &g_rs7_resume_PUBGB, nullptr, Full),
    RS7_PUB_MID(PUBGA, nullptr, nullptr, Full),
    RS7_PUB_MID(PUBGF, nullptr, nullptr, Full),
    RS7_PUB_MID(PUBEDGE, &g_rs7_resume_PUBEDGE, nullptr, Light),
    RS7_PUB_MID(PUBNL, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBAN, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBAC, nullptr, nullptr, Light),
    RS7_PUB_ZERO("PUBZ0", &g_rs7_target_PUBZ0),
    RS7_PUB_ZERO("PUBZ1", nullptr),
    RS7_PUB_ZERO("PUBZ2", nullptr),
    RS7_PUB_ZERO("PUBZ3", nullptr),
    RS7_PUB_ZERO("PUBZ4", &g_rs7_target_PUBZ4),
    RS7_PUB_ZERO("PUBZ5", &g_rs7_target_PUBZ5),
    RS7_PUB_ZERO("PUBZ6", &g_rs7_target_PUBZ6),
};

#undef RS7_PUB_ZERO
#undef RS7_PUB_MID
#undef RS7_PUB_ROW
}  // namespace

const Rs7HookTable kRs7PublishHooks = {Rs7_Group(Rs7Feature::Publish), kHooks, sizeof kHooks / sizeof kHooks[0]};

extern "C" {
void Rs7_Mid_LSA(Rs7Mid::Context&);  // loadprobe.cpp
void Rs7_Mid_LSG(Rs7Mid::Context&);
void Rs7_Mid_LSC(Rs7Mid::Context&);
void Rs7_Mid_LSE(Rs7Mid::Context&);
}

namespace {
// Rnd_Guitar_LoadStep probes (loadprobe.cpp), diagnostic: written only with [debug] publish_trace=1. Group Core, as
// before the move.
const Rs7Hook kProbeHooks[] = {
    {"LSA", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSA},
    {"LSG", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSG},
    {"LSC", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSC},
    {"LSE", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_LSE},
};
}  // namespace

const Rs7HookTable kRs7LoadProbeHooks = {Rs7_Group(Rs7Feature::Core), kProbeHooks,
                                         sizeof kProbeHooks / sizeof kProbeHooks[0]};
