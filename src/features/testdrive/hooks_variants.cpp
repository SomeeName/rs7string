// The test variants' mid-hooks TVCB / TVGP (testdrive.h): they give the game's runtime copies of the profile's settings
// the forced values. The profile's own settings store is never written.
#include "features/colours/colours.h"
#include "features/testdrive/testdrive.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <cstdint>

namespace {
constexpr uint32_t kGpLefty = 0x11, kGpColourblind = 0x15;  // GuitarProfile bytes (Rnd_GuitarProfile_Load 0x593B00)
constexpr uint32_t kCmMode = 0x348, kPlayers = 2;           // ColourManager colorMode[2] (0x519680)

unsigned long g_modeResets = 0;  // RsxrVariant_OnFrame's writes (Present thread only)

// The NUL-terminated text at `src` (at most cap - 1 characters, "?" when unreadable).
void ReadText(uintptr_t src, char* out, size_t cap) {
    size_t i = 0;

    for (; src && i + 1 < cap; ++i) {
        if (!Rsxr_SafeCopy(&out[i], reinterpret_cast<const void*>(src + i), 1) || !out[i]) {
            break;
        }
    }

    out[i] = 0;

    if (!i) {
        out[0] = '?';
        out[1] = 0;
    }
}
}  // namespace

// TVCB 0x519687 in Rnd_ColorMgr_SetColorBlindMode 0x519680, before its first _stricmp: EBX = the profile's
// ColorBlindMode text, ESI = the player, EDI = the colour manager. Both callers (profile load 0x519750, 0x601820) pass
// a temporary copy of the setting, so swapping EBX changes only the manager's mode (+348h + 4 * player).
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_TVCB(RsxrMid::Context& context) {
    const RsxrVariant& v = RsxrVariant_Get();
    char was[32];

    ReadText(context.ebx, was, sizeof was);

    const char* text = RsxrVariant_ColourModeText(v, was);

    if (text != was) {
        context.ebx = uint32_t(reinterpret_cast<uintptr_t>(text));
    }

    RsxrLog("variant: colour mode of player %u: profile \"%s\" -> %s (test override)", unsigned(context.esi), was,
            v.colourblind     ? "colourblind"
            : v.normalColours ? "normal"
                              : "unchanged");
}

// TVGP 0x593D4A in Rnd_GuitarProfile_Load 0x593B00, before `mov [esi+13h], al`: ESI = the runtime GuitarProfile
// (+11h LeftHandGuitar, +13h in-line headstock from GuitarTypeMP, +15h colourblind), AL = the profile's in-line flag.
// Rnd_InstrumentInfo_FromProfile 0x58ABD0 copies +11h / +13h into InstrumentInfo +9 / +0Ah.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_TVGP(RsxrMid::Context& context) {
    auto* gp = reinterpret_cast<uint8_t*>(uintptr_t(context.esi));
    const RsxrGuitarFlags was{gp[kGpLefty], uint8_t(context.eax & 0xFF), gp[kGpColourblind]};
    const RsxrGuitarFlags now = RsxrVariant_Apply(RsxrVariant_Get(), was);

    gp[kGpLefty] = now.lefty;
    gp[kGpColourblind] = now.colourblind;
    context.eax = (context.eax & ~0xFFu) | now.inlineHead;
    RsxrLog("variant: guitar profile lefty %u -> %u, in-line %u -> %u, colourblind %u -> %u (test override, not saved)",
            unsigned(was.lefty), unsigned(now.lefty), unsigned(was.inlineHead), unsigned(now.inlineHead),
            unsigned(was.colourblind), unsigned(now.colourblind));
}

// The overlay's Present callback (testdrive.h): the colour manager's mode back to the forced value.
void RsxrVariant_OnFrame() {
    const RsxrVariant& v = RsxrVariant_Get();
    uintptr_t mgr = 0;

    if (!v.colourblind || !g_rsxr_colormgr ||
        !Rsxr_SafeCopy(&mgr, reinterpret_cast<const void*>(g_rsxr_colormgr), sizeof mgr) || !mgr) {
        return;
    }

    for (uint32_t player = 0; player < kPlayers; ++player) {
        auto* mode = reinterpret_cast<uint32_t*>(mgr + kCmMode + 4 * player);
        uint32_t was = 0;

        if (!Rsxr_SafeCopy(&was, mode, sizeof was)) {
            return;
        }

        const uint32_t now = RsxrVariant_ColourMode(v, was);

        if (now == was) {
            continue;
        }

        *mode = now;

        if (++g_modeResets == 1) {
            RsxrLog("variant: colour mode of player %u was %u at a frame's end (before the profile load, or RSMods' "
                    "Extended Range flag) - colourblind written back whenever a frame ends with another mode (test "
                    "override)",
                    unsigned(player), unsigned(was));
        }
    }
}
