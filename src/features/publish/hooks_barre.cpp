// The finger marker barres on an extended neck (PUBBC, hooks_gridsites.cpp): the stock barre clip ends at six guitar
// strings at the guitar gap, so its seek and the capsule's scale are fitted to the span at the neck's string gap.
#include "features/frets/frets.h"
#include "features/publish/hooks.h"
#include "features/spacing/spacing.h"
#include "model/instrument.h"
#include "platform/seh.h"
#include <windows.h>

namespace {
constexpr float kGuitarGap = 1.608f;  // the stock string gaps (features/spacing)
constexpr float kBassGap = 2.68f;
constexpr float kClipGap = 1.608f;  // the barre clip at its end spans five guitar gaps (8.04, the stock neck width)

// The string gap of an extended instrument's neck (owner +6Ch the player, its guitar's InstrumentInfo), 0 otherwise.
float ExtendedGap(const uint8_t* owner) {
    const uint8_t* player = owner ? *reinterpret_cast<const uint8_t* const*>(owner + 0x6C) : nullptr;
    const uint8_t* guitar = player ? *reinterpret_cast<const uint8_t* const*>(player + rsxr::kSessionGuitar) : nullptr;
    const uint8_t* info = rsxr::GuitarInfo(guitar);

    if (!info) {
        return 0.0f;
    }

    const bool bass = rsxr::IsBassKind(info[rsxr::kInfoKind]);

    if (info[rsxr::kInfoStrings] <= (bass ? 4 : 6)) {
        return 0.0f;
    }

    return (bass ? kBassGap : kGuitarGap) * RsxrSpacing_Factor(info);
}

void BarreFit(const uint8_t* owner, uint8_t* const* marker, float* duration) {
    const float gap = ExtendedGap(owner);

    if (!(gap > 0.0f)) {
        return;
    }

    const int span = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(marker) + 0x1C);

    *duration *= rsxrpubgrid::BarreClipScale(span, gap, kClipGap);

    const float k = rsxrpubgrid::BarreEntityScale(span, gap, kClipGap);

    for (int i = 1; i <= 2; ++i) {
        RsxrFrets_SetScale(marker[i], k);
    }
}
}  // namespace

// `owner` and `marker` (+4 / +8 the capsule entities, +1Ch the span) of the marker update 0x58C690 when the span
// changed, `duration` its barre clip length (the seek is clamp((span - 1) / 5, 0, 1) of it). On an extended
// instrument the clip is seeked to the barre's real length at the neck's string gap, and the capsule is scaled up
// where that is longer than the clip (wider than the stock neck).
extern "C" void __cdecl RsxrPublish_BarreFit(const uint8_t* owner, uint8_t* marker, float* duration) {
    __try {
        if (owner && marker && duration) {
            BarreFit(owner, reinterpret_cast<uint8_t* const*>(marker), duration);
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }
}
