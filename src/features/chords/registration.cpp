// The chord loops' hooks ([chords] native_chordloops, group Chords): looked up by src/registry.cpp (Rs7_FindHook)
// and written by the main batch, all together and only with native_chordloops and native_strings (Rs7_ShouldPatch).
// The five jmp sites (CLBM CLBA CLGU CLGE CLVB) are light mid-hooks (hooks_cl.cpp); the others have a null stub,
// they are in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/chords/chords.h"
#include "model/groups.h"

extern "C" void Rs7_Mid_B1(Rs7Mid::Context&);  // hooks_frames.cpp
extern "C" void Rs7_Stub_B2();                 // hooks_stubs.cpp
extern "C" uintptr_t g_rs7_target_B2, g_rs7_fn_sst;

extern "C" {
#define CL_DECL(id)                                                                                                    \
    void Rs7_Mid_##id(Rs7Mid::Context&);                                                                               \
    extern uintptr_t g_rs7_resume_##id;
CL_DECL(CLBM) CL_DECL(CLBA) CL_DECL(CLGU) CL_DECL(CLGE) CL_DECL(CLVB)
#undef CL_DECL
}

namespace {
#define CL_MID(id)                                                                                                     \
    {#id, nullptr, &g_rs7_resume_##id, nullptr, false, false, false, 0, 0, &Rs7_Mid_##id, 0, 0,                        \
     0,   false,   Rs7Mid::Mode::Light}
const Rs7Hook kHooks[] = {
    {"CLB0", nullptr, nullptr, nullptr, false, false},
    CL_MID(CLBM),
    {"CLBF", nullptr, nullptr, nullptr, false, false},
    {"CLB1", nullptr, nullptr, nullptr, false, false},
    {"CLBG", nullptr, nullptr, nullptr, false, false},
    CL_MID(CLBA),
    CL_MID(CLGU),
    CL_MID(CLGE),
    CL_MID(CLVB),
    {"CLNF2", nullptr, nullptr, nullptr, false, false},
    {"CLNF3", nullptr, nullptr, nullptr, false, false},
    {"CLCT4", nullptr, nullptr, nullptr, false, false},
    {"CLCT5", nullptr, nullptr, nullptr, false, false},
    {"CLCT6", nullptr, nullptr, nullptr, false, false},
    {"CLCT7", nullptr, nullptr, nullptr, false, false},
    {"CLCT8", nullptr, nullptr, nullptr, false, false},
    {"CLCT9", nullptr, nullptr, nullptr, false, false},
    {"CLCT10", nullptr, nullptr, nullptr, false, false},
    {"CLCT11", nullptr, nullptr, nullptr, false, false},
};
#undef CL_MID

const Rs7Hook kFrameHooks[] = {
    {"B1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_B1},
    {"X_SST", nullptr, nullptr, &g_rs7_fn_sst, false, true},
    {"B2", reinterpret_cast<void*>(&Rs7_Stub_B2), nullptr, &g_rs7_target_B2, false, false},
};
}  // namespace

extern const Rs7HookTable kRs7ChordHooks = {Rs7_Group(Rs7Feature::Chords), kHooks, sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7ChordFrameHooks = {Rs7_Group(Rs7Feature::Core), kFrameHooks,
                                                 sizeof kFrameHooks / sizeof kFrameHooks[0]};
