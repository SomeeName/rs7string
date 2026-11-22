// TF1 - 0x5B26B5 in the tuner camera pose 0x5B2680 (EAX = the guitar; not inverted): the stock reference string
// `mov edx,[eax+24h]; mov bl,[edx+0Bh]; dec bl` = count-1, the string at the edge opposite string 0. On a guitar built
// with 7/8 strings, strings 6/7 lie beyond string 0 (the extrapolated lanes), so that edge is still string 5:
// BL = min(count, 6) - 1. A five-string bass's string 4 is its low B, next to string 0: BL = 3 (the G; without it the
// view sat too low and co-op cut off the bass's D and G pegs, 2026-10-31). Four- and six-string bass, six-string
// guitar: unchanged. Back to 0x5B26BD.
//
// Light mid-hook (per tuner frame, integer only; LIGHT_CALLBACKS in tools/tests/emu_harness.py), converted from a
// naked stub (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_cold.py). The old stub left the flags of
// its own `cmp bl,6; dec bl`; no continuation reads them (contract "free").
#include "features/tuner/hooks.h"
#include "model/instrument.h"
#include "platform/midhook.h"
#include <cstdint>

using RsxrMid::Dword;

extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_TF1 = 0;

namespace {
constexpr uint8_t kEdgeStrings = 6;
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_TF1(RsxrMid::Context& c) {
    c.edx = Dword(c.eax + rsxr::kGuitarInfo);
    const auto* info = reinterpret_cast<const uint8_t*>(c.edx);
    const uint8_t count = info[rsxr::kInfoStrings];
    // A five-string bass's string 4 is its low B, next to string 0: the opposite edge stays the G (string 3).
    const uint8_t edge = rsxr::IsBassKind(info[rsxr::kInfoKind]) && count == 5 ? 4
                         : count > kEdgeStrings                                ? kEdgeStrings
                                                                               : count;

    c.ebx = (c.ebx & 0xFFFFFF00u) | static_cast<uint8_t>(edge - 1);
    c.jump = static_cast<uint32_t>(g_rsxr_resume_TF1);
}

// TF2 - 0x5B297F in the same pose, on the overview offset table's path (table 0x12EFC68; the other table 0x12EFDE8 is
// left alone), after the camera position's world y ([EBP-24h], along the neck) took its offset. ESI = the tuner
// component (+2Ch the guitar). The stock framing fits six pegs; a right-handed guitar built with 7/8 strings has a
// longer in-line headstock whose outer pegs and letters fell off the left edge (CONFIRMED in game 2026-10-29), so
// the camera pans kPan units towards the tip. The same for a 5/6-string bass on its in-line model (the 6-string's high
// C peg was cut off, CONFIRMED in game 2026-10-31). Left-handed views have the room (mirrored framing). Full mode
// (x87). The displaced FLD is replayed after the callback.
namespace {
constexpr uint32_t kInfoLefty = 0x09;  // InstrumentInfo: u8 non-zero = left-handed (the pose's own read)
constexpr float kPan = 6.0f;           // tested 2026-10-29: 6 shows the 8th peg and the high e letter with a margin
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_TF2(RsxrMid::Context& c) {
    const uint32_t guitar = Dword(c.esi + rsxrtu::kGuitar);
    const uint32_t info = guitar ? Dword(guitar + rsxr::kGuitarInfo) : 0;
    if (!info) {
        return;
    }

    const auto* i = reinterpret_cast<const uint8_t*>(info);
    const bool guitarClass = !rsxr::IsBassKind(i[rsxr::kInfoKind]);
    const bool lefty = i[kInfoLefty] != 0;

    const bool extended =
        guitarClass ? rsxr::IsExtendedGuitar(i[rsxr::kInfoStrings]) : rsxr::IsExtendedBass(i[rsxr::kInfoStrings]);

    if (!lefty && extended) {
        *reinterpret_cast<float*>(c.ebp - 0x24) -= kPan;
    }
}
