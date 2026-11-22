#include "features/tones/tones.h"
#include "model/gates.h"
#include "model/groups.h"

uintptr_t g_rsxr_tones = 1;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_tone_apply = 0, g_rsxr_tone_manager = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_tone_register = 0, g_rsxr_tone_destroy = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_tone_context = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_TonePlanField = 0, g_rsxr_resume_TonePlanExtension = 0,
                                           g_rsxr_resume_TonePlanBound = 0;

extern "C" {
void Rsxr_Mid_ToneLoad(RsxrMid::Context&);
void Rsxr_Mid_ToneAutoBefore(RsxrMid::Context&);
void Rsxr_Mid_ToneOtherBefore(RsxrMid::Context&);
void Rsxr_Mid_ToneAutoAfter(RsxrMid::Context&);
void Rsxr_Mid_ToneOtherAfter(RsxrMid::Context&);
void Rsxr_Mid_ToneRefreshAfter(RsxrMid::Context&);
void Rsxr_Mid_ToneRegisterAfter(RsxrMid::Context&);
void Rsxr_Mid_TonePlanBefore(RsxrMid::Context&);
void Rsxr_Mid_TonePlanAfter(RsxrMid::Context&);
void Rsxr_Mid_TonePlanField(RsxrMid::Context&);
void Rsxr_Mid_TonePlanExtension(RsxrMid::Context&);
void Rsxr_Mid_TonePlanBound(RsxrMid::Context&);
void Rsxr_Mid_TonePlanEdit(RsxrMid::Context&);
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("ToneLoad", &Rsxr_Mid_ToneLoad).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneAuto"), {2}, &Rsxr_Mid_ToneAutoBefore, &Rsxr_Mid_ToneAutoAfter).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneSetup"), {2}, &Rsxr_Mid_ToneOtherBefore, &Rsxr_Mid_ToneOtherAfter).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneSeek"), {2}, &Rsxr_Mid_ToneOtherBefore, &Rsxr_Mid_ToneOtherAfter).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneResume"), {2}, &Rsxr_Mid_ToneOtherBefore, &Rsxr_Mid_ToneOtherAfter)
        .Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneOverride"), {2}, &Rsxr_Mid_ToneOtherBefore, &Rsxr_Mid_ToneOtherAfter)
        .Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneRefresh"), {2}, nullptr, &Rsxr_Mid_ToneRefreshAfter).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("ToneRegister"), {1}, nullptr, &Rsxr_Mid_ToneRegisterAfter).Gate(kRsxrGateTones),
    Rsxr_Ref("ToneApply", &g_rsxr_tone_apply).Gate(kRsxrGateTones),
    Rsxr_Ref("ToneManager", &g_rsxr_tone_manager).Gate(kRsxrGateTones),
    Rsxr_Ref("ToneRegisterCall", &g_rsxr_tone_register).Gate(kRsxrGateTones),
    Rsxr_Ref("ToneResourceDestroy", &g_rsxr_tone_destroy).Gate(kRsxrGateTones),
    Rsxr_WrapRow(Rsxr_Site("TonePlanBuild"), {2}, &Rsxr_Mid_TonePlanBefore, &Rsxr_Mid_TonePlanAfter)
        .Gate(kRsxrGateTones),
    Rsxr_Mid("TonePlanField", &Rsxr_Mid_TonePlanField, &g_rsxr_resume_TonePlanField).Gate(kRsxrGateTones),
    Rsxr_Mid("TonePlanExtension", &Rsxr_Mid_TonePlanExtension, &g_rsxr_resume_TonePlanExtension).Gate(kRsxrGateTones),
    Rsxr_Mid("TonePlanBound", &Rsxr_Mid_TonePlanBound, &g_rsxr_resume_TonePlanBound).Gate(kRsxrGateTones),
    Rsxr_Mid("TonePlanEdit", &Rsxr_Mid_TonePlanEdit).Gate(kRsxrGateTones),
    Rsxr_Ref("ToneContext", &g_rsxr_tone_context).Gate(kRsxrGateTones),
};
}

extern const RsxrHookTable kRsxrToneHooks = {Rsxr_Group(RsxrFeature::Tones), kHooks, sizeof kHooks / sizeof kHooks[0]};
