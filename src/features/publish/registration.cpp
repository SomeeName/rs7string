// The publication's hooks (group Publish), looked up by src/registry.cpp (Rs7_FindHook) and written by the main
// batch, only with the publication ports (Rs7_ShouldPatch: the rows' `publish` flag). PUBNN1 PUBNN3 PUBNN4 have a
// null stub: in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/publish/hooks.h"
#include "model/gates.h"
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
// A mid-hook row (light: per note / cell, integer only; full: the rest).
#define RS7_PUB_MID(ID, RESUME, TARGET, MODE)                                                                          \
    Rs7_Mid(#ID, &Rs7_Mid_##ID, RESUME, TARGET).Gate(kRs7GatePublish).InMode(Rs7Mid::Mode::MODE)
// PUBZ0-PUBZ6: one light callback (call sites before the callee, jmp sites with the relocated jmp as the replay).

const Rs7Hook kHooks[] = {
    // R1: entry of Rnd_FretGrid_GetCellPos 0x58FE00; with the native ports every query is answered from the native
    // cells (PUBGP), strings 0-7.
    Rs7_Mid("R1", &Rs7_Mid_PUBGP).Gate(kRs7GatePublish).Sse(),
    RS7_PUB_MID(PUBCL, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBTO, nullptr, nullptr, Light),
    RS7_PUB_MID(PUBWU, nullptr, nullptr, Light),
    Rs7_Site("PUBNN1").Gate(kRs7GatePublish),
    Rs7_Site("PUBNN3").Gate(kRs7GatePublish),
    Rs7_Site("PUBNN4").Gate(kRs7GatePublish),
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
    Rs7_Mid("PUBZ0", &Rs7_Mid_PUBZ, nullptr, &g_rs7_target_PUBZ0).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ1", &Rs7_Mid_PUBZ).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ2", &Rs7_Mid_PUBZ).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ3", &Rs7_Mid_PUBZ).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ4", &Rs7_Mid_PUBZ, nullptr, &g_rs7_target_PUBZ4).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ5", &Rs7_Mid_PUBZ, nullptr, &g_rs7_target_PUBZ5).Gate(kRs7GatePublish).Light(),
    Rs7_Mid("PUBZ6", &Rs7_Mid_PUBZ, nullptr, &g_rs7_target_PUBZ6).Gate(kRs7GatePublish).Light(),
};

#undef RS7_PUB_MID
}  // namespace

const Rs7HookTable kRs7PublishHooks = {Rs7_Group(Rs7Feature::Publish), kHooks, sizeof kHooks / sizeof kHooks[0]};

extern "C" {
void Rs7_Mid_LSA(Rs7Mid::Context&);  // loadprobe.cpp
void Rs7_Mid_LSG(Rs7Mid::Context&);
void Rs7_Mid_LSC(Rs7Mid::Context&);
void Rs7_Mid_LSE(Rs7Mid::Context&);
}

namespace {
// Rnd_Guitar_LoadStep probes (loadprobe.cpp), diagnostic: written only with [debug] publish_trace=1. Group Core.
const Rs7Hook kProbeHooks[] = {
    Rs7_Mid("LSA", &Rs7_Mid_LSA).Gate(kRs7GatePublishTrace),
    Rs7_Mid("LSG", &Rs7_Mid_LSG).Gate(kRs7GatePublishTrace),
    Rs7_Mid("LSC", &Rs7_Mid_LSC).Gate(kRs7GatePublishTrace),
    Rs7_Mid("LSE", &Rs7_Mid_LSE).Gate(kRs7GatePublishTrace),
};
}  // namespace

const Rs7HookTable kRs7LoadProbeHooks = {Rs7_Group(Rs7Feature::Core), kProbeHooks,
                                         sizeof kProbeHooks / sizeof kProbeHooks[0]};
