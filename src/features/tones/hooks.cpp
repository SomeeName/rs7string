#include "features/tones/game.h"
#include "features/tones/state.h"
#include "features/tones/tones.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"

namespace {
constexpr RsxrMid::Wrap kTimeline{2};
constexpr RsxrMid::Wrap kRefresh{2};

void TimelineAfter(RsxrMid::Context& c, bool automatic) {
    auto& frame = RsxrMid::Frame(c, kTimeline);
    const uint32_t session = frame.user;
    const bool event = (c.eax & 0xFF) != 0;
    const uint32_t raw = rsxrtones::Read(frame.Arg(1));
    const RsxrMid::GameFcw fcw(c);

    if (rsxrtones::Handle(session, event, raw, !automatic)) {
        // Own all selections in an extended session. UINT_MAX has no stock switch case; success keeps the
        // other consumers out of their Base fallback. F (5) and Base (26) never enter the stock sentinel logic.
        const uint32_t ignored = rsxrtones::kInvalid;

        if (Rsxr_SafeStore(reinterpret_cast<void*>(uintptr_t(frame.Arg(1))), &ignored, sizeof ignored)) {
            c.eax = (c.eax & 0xFFFFFF00u) | 1u;
        }
    }
}
}  // namespace

// 0x56774C: EAX = Attributes.Tones collection, [EBP+0Ch] = parent arrangement, [EBP-3A0h] = UUID.
extern "C" __declspec(dllexport) void Rsxr_Mid_ToneLoad(RsxrMid::Context& c) {
    rsxrtones::Identity id;
    const RsxrMid::GameFcw fcw(c);

    if (Rsxr_SafeCopy(id.data(), reinterpret_cast<const void*>(uintptr_t(c.ebp - 0x3A0)), sizeof id)) {
        rsxrtones::Capture(rsxrtones::Attributes(rsxrtones::Read(c.ebp, 0xC)), c.eax, id);
    } else {
        RsxrLog("tones26: manifest rejected: unreadable arrangement identity");
    }
}

extern "C" __declspec(dllexport) void Rsxr_Mid_ToneAutoBefore(RsxrMid::Context& c) {
    RsxrMid::Frame(c, kTimeline).user = c.esi;
}

extern "C" __declspec(dllexport) void Rsxr_Mid_ToneOtherBefore(RsxrMid::Context& c) {
    RsxrMid::Frame(c, kTimeline).user = c.ebx;
}

extern "C" __declspec(dllexport) void Rsxr_Mid_ToneAutoAfter(RsxrMid::Context& c) {
    TimelineAfter(c, true);
}

extern "C" __declspec(dllexport) void Rsxr_Mid_ToneOtherAfter(RsxrMid::Context& c) {
    TimelineAfter(c, false);
}

extern "C" __declspec(dllexport) void Rsxr_Mid_ToneRegisterAfter(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);

    rsxrtones::PreRegister(RsxrMid::Frame(c, {1}).Arg(0));
}

// 0x7DF400 can deliberately bypass the accessor (force=true). Preserve its work, then restore the timeline tone.
extern "C" __declspec(dllexport) void Rsxr_Mid_ToneRefreshAfter(RsxrMid::Context& c) {
    auto& frame = RsxrMid::Frame(c, kRefresh);
    if (!(frame.Arg(1) & 0xFF)) {
        return;
    }

    const uint32_t session = frame.Arg(0), song = rsxrtones::Read(session, 0x78);
    const uint32_t begin = rsxrtones::Read(song, 0xDC), end = rsxrtones::Read(song, 0xE0);
    if (end < begin || (end - begin) % 8 || (end - begin) / 8 > 1000000) {
        return;
    }

    const float time = Rsxr_SafeRead<float>(reinterpret_cast<const void*>(uintptr_t(session)), 0x3B4);
    uint32_t raw = 0;
    bool event = false;

    for (uint32_t at = end; at > begin;) {
        at -= 8;

        if (Rsxr_SafeRead<float>(reinterpret_cast<const void*>(uintptr_t(at)), 0) < time) {
            raw = rsxrtones::Read(at, 4);
            event = true;
            break;
        }
    }

    const RsxrMid::GameFcw fcw(c);

    (void)rsxrtones::Handle(session, event, raw, true);
}
