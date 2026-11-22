#include "features/headstock/pegcolour.h"
#include "features/colours/colours.h"
#include "features/headstock/pegset.h"
#include "model/groups.h"
#include <cstring>

unsigned rsxrpegcolour::Refresh(uint8_t* pegs, const uint8_t* manager, uint32_t player) {
    if (!g_rsxr_native_colours || !pegs || !manager || player >= 2) {
        return 0;
    }

    const unsigned mode = rsxrhead::ColourMode(manager, player);
    const unsigned count = *reinterpret_cast<const uint16_t*>(pegs + rsxrhead::kPegCount);
    if (mode >= 2 || count > rsxrhead::kCapacity) {
        return 0;
    }

    unsigned changed = 0;

    for (unsigned s = 0; s < count; ++s) {
        auto* slot = pegs + rsxrhead::kSlots + s * rsxrhead::kSlotStride;
        if (!slot[0]) {
            continue;
        }

        // CONFIRMED 59BD70: names 0/1 are inactive/success (states 6/5),
        // names 2..7 are state-4 string colours; PSN2 adds names 8/9 for extras.
        const unsigned name = *reinterpret_cast<const uint32_t*>(slot + rsxrhead::kSlotNamed);
        if (name > 9) {
            continue;
        }

        const float* source = nullptr;
        float extra[3], palette[6];

        // Extra RGB pointers are owned snapshots. RSMods can change the stock brightness after
        // publication, so names 8/9 take PSN2's live ratio and kPegPeak cap (rsxrhead::NamedColour).
        if (name >= 8) {
            source = rsxrhead::NamedColour(manager, player, ExtraPalette(name - 2, palette), extra) ? extra : nullptr;
        } else {
            const unsigned state = name < 2 ? 6 - name : 4;
            const unsigned string = name < 2 ? 0 : name - 2;

            source = *reinterpret_cast<const float* const*>(manager + rsxrcolour::PtrOffset(mode, state, string));
        }

        if (source && std::memcmp(slot + rsxrhead::kSlotRgb, source, 12)) {
            std::memcpy(slot + rsxrhead::kSlotRgb, source, 12);
            ++changed;
        }
    }

    return changed;
}

const float* rsxrpegcolour::ExtraPalette(uint32_t index, float buf[6]) {
    if (index != uint32_t(rsxrhead::kStockSlots) + 1) {
        return g_rsxr_fx_rgb;
    }

    for (uint32_t mode = 0; mode < 2; ++mode) {
        Rsxr_RgbToFloats(rsxrcolour::Colour8(mode), buf + 3 * mode);
    }

    return buf;
}
