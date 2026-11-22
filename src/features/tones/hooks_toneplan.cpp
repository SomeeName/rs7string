#include "features/tones/game.h"
#include "features/tones/toneplan.h"
#include "model/gamestring.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <cstring>

extern "C" uintptr_t g_rsxr_resume_TonePlanField, g_rsxr_resume_TonePlanExtension;
extern "C" uintptr_t g_rsxr_resume_TonePlanBound;

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanBefore(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);
    auto& frame = RsxrMid::Frame(c, {2});

    frame.user = rsxrtones::BeginPlan(frame.Arg(1));
}

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanAfter(RsxrMid::Context& c) {
    rsxrtones::EndPlan(RsxrMid::Frame(c, {2}).user);
}

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanField(RsxrMid::Context& c) {
    if (rsxrtones::PlanActive() && c.eax % 0x18 == 0) {
        const uint32_t name = rsxrtones::PlanName(c.eax / 0x18, true);

        if (name) {
            c.edi = name;
            c.jump = static_cast<uint32_t>(g_rsxr_resume_TonePlanField);
        }
    }
}

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanExtension(RsxrMid::Context& c) {
    if (rsxrtones::PlanActive() && c.eax % 0x18 == 0) {
        const uint32_t name = rsxrtones::PlanName(c.eax / 0x18, false);

        if (name) {
            c.eax = name;
            c.jump = static_cast<uint32_t>(g_rsxr_resume_TonePlanExtension);
        }
    }
}

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanBound(RsxrMid::Context& c) {
    if (rsxrtones::PlanActive()) {
        c.Compare32(c.eax, 27);
        // The displaced CMP/JL pair's signed comparison. Derive the back edge from the resolved fallthrough.
        const uint32_t resume = static_cast<uint32_t>(g_rsxr_resume_TonePlanBound);

        c.jump = static_cast<int32_t>(c.eax) < 27 ? resume - 0xEC8 : resume;
    }
}

extern "C" __declspec(dllexport) void Rsxr_Mid_TonePlanEdit(RsxrMid::Context& c) {
    char extension[16]{}, key[256]{};

    if (rsxr::ReadGameString(reinterpret_cast<void*>(uintptr_t(c.esp + 0x4C)), extension, sizeof extension) &&
        std::strlen(extension) == 5 && std::strncmp(extension, "Tone", 4) == 0 && extension[4] >= 'E' &&
        extension[4] <= 'Z') {
        (void)rsxr::ReadGameString(reinterpret_cast<void*>(uintptr_t(c.esp + 0x94)), key, sizeof key);
        RsxrLog("tones26: toneplan edit slot=%c key='%s' stock chooser", extension[4], key);
    }
}
