// The publication's hooks (group Publish), looked up by src/registry.cpp (Rsxr_FindHook) and written by the main
// batch, only with the publication ports (Rsxr_ShouldPatch: the rows' `publish` flag). PUBNN1 PUBNN3 PUBNN4 have a
// null stub: in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/publish/hooks.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_PUBGP(RsxrMid::Context&);  // hooks_gridsites.cpp (site R1, sse mode)
#define PUB_DECL(id) void Rsxr_Mid_##id(RsxrMid::Context&);
#define PUB_DECL_RESUME(id) PUB_DECL(id) extern uintptr_t g_rsxr_resume_##id;
#define PUB_DECL_GUARD(id) PUB_DECL_RESUME(id) extern uintptr_t g_rsxr_target_##id;
// clang-format off
PUB_DECL(PUBCL) PUB_DECL(PUBTO) PUB_DECL(PUBWU) PUB_DECL(PUBNL) PUB_DECL(PUBAN) PUB_DECL(PUBAC)  // hooks_entries.cpp
PUB_DECL_GUARD(PUBGS) PUB_DECL_GUARD(PUBGE1) PUB_DECL_GUARD(PUBGE2)  // hooks_gridsites.cpp
PUB_DECL_RESUME(PUBGC) PUB_DECL_RESUME(PUBGO) PUB_DECL_RESUME(PUBGB) PUB_DECL(PUBGA) PUB_DECL(PUBGF) PUB_DECL(PUBFA) PUB_DECL(PUBBC)
PUB_DECL_RESUME(PUBEDGE)
PUB_DECL(PUBZ)  // hooks_pitch.cpp: PUBZ0-PUBZ6, the callees of the call sites
extern uintptr_t g_rsxr_target_PUBZ0, g_rsxr_target_PUBZ4, g_rsxr_target_PUBZ5, g_rsxr_target_PUBZ6;
// clang-format on
#undef PUB_DECL_GUARD
#undef PUB_DECL_RESUME
#undef PUB_DECL
}

namespace {
// A mid-hook row (light: per note / cell, integer only; full: the rest).
#define RSXR_PUB_MID(ID, RESUME, TARGET, MODE)                                                                         \
    Rsxr_Mid(#ID, &Rsxr_Mid_##ID, RESUME, TARGET).Gate(kRsxrGatePublish).InMode(RsxrMid::Mode::MODE)
// PUBZ0-PUBZ6: one light callback (call sites before the callee, jmp sites with the relocated jmp as the replay).

const RsxrHook kHooks[] = {
    // R1: entry of Rnd_FretGrid_GetCellPos 0x58FE00; with the native ports every query is answered from the native
    // cells (PUBGP), strings 0-7.
    Rsxr_Mid("R1", &Rsxr_Mid_PUBGP).Gate(kRsxrGatePublish).Sse(),
    RSXR_PUB_MID(PUBCL, nullptr, nullptr, Light),
    RSXR_PUB_MID(PUBTO, nullptr, nullptr, Light),
    RSXR_PUB_MID(PUBWU, nullptr, nullptr, Light),
    Rsxr_Site("PUBNN1").Gate(kRsxrGatePublish),
    Rsxr_Site("PUBNN3").Gate(kRsxrGatePublish),
    Rsxr_Site("PUBNN4").Gate(kRsxrGatePublish),
    RSXR_PUB_MID(PUBGS, &g_rsxr_resume_PUBGS, &g_rsxr_target_PUBGS, Light),
    RSXR_PUB_MID(PUBGE1, &g_rsxr_resume_PUBGE1, &g_rsxr_target_PUBGE1, Light),
    RSXR_PUB_MID(PUBGE2, &g_rsxr_resume_PUBGE2, &g_rsxr_target_PUBGE2, Light),
    RSXR_PUB_MID(PUBGC, &g_rsxr_resume_PUBGC, nullptr, Light),
    RSXR_PUB_MID(PUBGO, &g_rsxr_resume_PUBGO, nullptr, Light),
    RSXR_PUB_MID(PUBGB, &g_rsxr_resume_PUBGB, nullptr, Full),
    RSXR_PUB_MID(PUBGA, nullptr, nullptr, Full),
    RSXR_PUB_MID(PUBGF, nullptr, nullptr, Full),
    RSXR_PUB_MID(PUBFA, nullptr, nullptr, Light),
    RSXR_PUB_MID(PUBBC, nullptr, nullptr, Full),
    RSXR_PUB_MID(PUBEDGE, &g_rsxr_resume_PUBEDGE, nullptr, Light),
    RSXR_PUB_MID(PUBNL, nullptr, nullptr, Light),
    RSXR_PUB_MID(PUBAN, nullptr, nullptr, Light),
    RSXR_PUB_MID(PUBAC, nullptr, nullptr, Light),
    Rsxr_Mid("PUBZ0", &Rsxr_Mid_PUBZ, nullptr, &g_rsxr_target_PUBZ0).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ1", &Rsxr_Mid_PUBZ).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ2", &Rsxr_Mid_PUBZ).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ3", &Rsxr_Mid_PUBZ).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ4", &Rsxr_Mid_PUBZ, nullptr, &g_rsxr_target_PUBZ4).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ5", &Rsxr_Mid_PUBZ, nullptr, &g_rsxr_target_PUBZ5).Gate(kRsxrGatePublish).Light(),
    Rsxr_Mid("PUBZ6", &Rsxr_Mid_PUBZ, nullptr, &g_rsxr_target_PUBZ6).Gate(kRsxrGatePublish).Light(),
};

#undef RSXR_PUB_MID
}  // namespace

const RsxrHookTable kRsxrPublishHooks = {Rsxr_Group(RsxrFeature::Publish), kHooks, sizeof kHooks / sizeof kHooks[0]};

extern "C" {
void Rsxr_Mid_LSA(RsxrMid::Context&);  // loadprobe.cpp
void Rsxr_Mid_LSG(RsxrMid::Context&);
void Rsxr_Mid_LSC(RsxrMid::Context&);
void Rsxr_Mid_LSE(RsxrMid::Context&);
}

namespace {
// Rnd_Guitar_LoadStep probes (loadprobe.cpp), diagnostic: written only with [debug] publish_trace=1. Group Core.
const RsxrHook kProbeHooks[] = {
    Rsxr_Mid("LSA", &Rsxr_Mid_LSA).Gate(kRsxrGatePublishTrace),
    Rsxr_Mid("LSG", &Rsxr_Mid_LSG).Gate(kRsxrGatePublishTrace),
    Rsxr_Mid("LSC", &Rsxr_Mid_LSC).Gate(kRsxrGatePublishTrace),
    Rsxr_Mid("LSE", &Rsxr_Mid_LSE).Gate(kRsxrGatePublishTrace),
};
}  // namespace

const RsxrHookTable kRsxrLoadProbeHooks = {Rsxr_Group(RsxrFeature::Core), kProbeHooks,
                                           sizeof kProbeHooks / sizeof kProbeHooks[0]};
