#include "core/pegcolour.h"
#include "core/colournative.h"
#include "core/headstock.h"
#include "hooks/registry.h"
#include <cstring>

unsigned rs7pegcolour::Refresh(uint8_t* pegs, const uint8_t* manager, uint32_t player) {
    if (!g_rs7_native_colours || !pegs || !manager || player >= 2) {
        return 0;
    }

    const unsigned mode = *reinterpret_cast<const uint32_t*>(manager + 0x348 + 4 * player);
    const unsigned count = *reinterpret_cast<const uint16_t*>(pegs + rs7head::kPegCount);
    if (mode >= 2 || count > rs7head::kCapacity) {
        return 0;
    }

    unsigned changed = 0;

    for (unsigned s = 0; s < count; ++s) {
        auto* slot = pegs + rs7head::kSlots + s * rs7head::kSlotStride;
        if (!slot[0]) {
            continue;
        }

        // CONFIRMED 59BD70: names 0/1 are inactive/success (states 6/5),
        // names 2..7 are state-4 string colours; PSN2 adds names 8/9 for extras.
        const unsigned name = *reinterpret_cast<const uint32_t*>(slot + rs7head::kSlotNamed);
        if (name > 9) {
            continue;
        }

        const unsigned state = name < 2 ? 6 - name : 4;
        const unsigned string = name < 2 ? 0 : name - 2;
        const float* source =
            *reinterpret_cast<const float* const*>(manager + rs7colour::PtrOffset(mode, state, string));
        float extra[3];

        // Extra RGB pointers are owned snapshots. RSMods can change the stock
        // brightness after publication, so use the same live ratio as PSN2.
        if (name == 8) {
            source = rs7head::NamedColour(manager, player, g_rs7_fx_rgb, extra) ? extra : nullptr;
        }

        if (source && std::memcmp(slot + rs7head::kSlotRgb, source, 12)) {
            std::memcpy(slot + rs7head::kSlotRgb, source, 12);
            ++changed;
        }
    }

    return changed;
}
