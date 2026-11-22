// The native GuitarStrings / InstrumentInfo layout (strings.h): values in, values out.
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "platform/safecopy.h"
#include <cstring>

using namespace rsxrgs;

// Called from mid-hooks (no SEH frame) with the game's pointers: both reads are guarded, a fault means "not native".
bool rsxrgs::IsNative(const uint8_t* gs) {
    uint16_t marker = 0;
    uint32_t magic = 0;

    return gs && Rsxr_SafeCopy(&marker, gs + kMarkerOff, sizeof marker) && marker == kMarker &&
           Rsxr_SafeCopy(&magic, gs + kMagicOff, sizeof magic) && magic == kMagic;
}

void rsxrgs::InitTail(uint8_t* gs, uint8_t* guitar, uint8_t* info, uint8_t* const extraMain[2],
                      uint8_t* const extraGhost[2]) {
    for (int s = 0; s < kStock; ++s) {
        UncheckedField<uint8_t*>(gs, kMain + 4 * s) = UncheckedRead<uint8_t*>(gs, 0x04 + 4 * s);
        UncheckedField<uint8_t*>(gs, kGhost + 4 * s) = UncheckedRead<uint8_t*>(gs, 0x1C + 4 * s);
    }

    for (int i = 0; i < 2; ++i) {
        UncheckedField<uint8_t*>(gs, kMain + 4 * (kStock + i)) = extraMain[i];
        UncheckedField<uint8_t*>(gs, kGhost + 4 * (kStock + i)) = extraGhost[i];
    }

    std::memset(gs + kGlow, 0, 4 * kCapacity);
    UncheckedField<uint8_t>(gs, kTail + 0) = 0;  // frames without the 7th string
    UncheckedField<uint8_t>(gs, kTail + 1) = 0;  // load generation
    UncheckedField<uint8_t>(gs, kTail + 2) = 0;  // culled generation
    bool complete = true;

    for (int i = 0; i < 2; ++i) {
        complete = complete && extraMain[i] && extraGhost[i];
    }

    UncheckedField<uint8_t>(gs, kTail + 3) = complete ? 1 : 0;
    UncheckedField<uint8_t*>(gs, kGuitar) = guitar;
    UncheckedField<uint8_t*>(gs, kInfo) = info;
    UncheckedField<uint16_t>(gs, kMarkerOff) = kMarker;
    UncheckedField<uint32_t>(gs, kMagicOff) = kMagic;
}

void rsxrgs::RefreshCounts(uint8_t* info, bool seventh) {
    const uint8_t stock = UncheckedRead<uint8_t>(info, rsxr::kInfoStrings);

    if (g_rsxr_publish_ports) {
        // C-lite: +B is the count the guitar was BUILT with (4 bass, 6, 7 or 8) and stays so; the
        // capacity of 8 string objects is preloaded for every guitar, the active count is the build count.
        // A bass (+0 = 2; 4, or 5 / 6 extended) loads and shows exactly its build count: its attach points end there.
        if (rsxr::IsBassKind(UncheckedRead<uint8_t>(info, rsxr::kInfoKind))) {
            const uint8_t n = stock < 4 ? 4 : stock > 6 ? 6 : stock;

            UncheckedField<uint8_t>(info, kInfoLoaded) = UncheckedField<uint8_t>(info, kInfoActive) = n;

            // perStringMode (+20h + 4 * string, the bend direction) of the added strings: a stock bass never sets
            // entries 4 / 5, so the low B bent the wrong way. The low B bends like the E next to it, the high C like
            // the G (rsxr::kBassNeighbour).
            for (int s = 4; s < n; ++s) {
                UncheckedField<uint32_t>(info, 0x20 + 4 * s) =
                    UncheckedRead<uint32_t>(info, 0x20 + 4 * rsxr::kBassNeighbour[s]);
            }

            return;
        }

        UncheckedField<uint8_t>(info, kInfoLoaded) = stock == 4 ? 4 : 8;
        UncheckedField<uint8_t>(info, kInfoActive) = stock == 4 ? 4 : stock < 6 ? 6 : stock > 8 ? 8 : stock;
        // The native tail's mode[6] / mode[7] (+38h / +3Ch) would be a mode no stock string has when zero:
        // mirror string 0's (the BD1-BD4 hooks do the same; HYPOTHESIS: no unguarded reader found, this keeps
        // any such reader on a valid stock value). Once, when the guitar's info is first refreshed.
        if (stock != 4 && !UncheckedRead<uint32_t>(info, 0x38) && !UncheckedRead<uint32_t>(info, 0x3C)) {
            UncheckedField<uint32_t>(info, 0x38) = UncheckedField<uint32_t>(info, 0x3C) =
                UncheckedRead<uint32_t>(info, 0x20);
        }
        return;
    }

    uint8_t loaded = stock == kStock && seventh ? uint8_t(kStock + 1) : stock;

    if (loaded > kCapacity) {
        loaded = kCapacity;
    }
    uint8_t active = UncheckedRead<uint8_t>(info, kInfoActive);
    const uint8_t lo = stock < loaded ? stock : loaded;

    if (active < lo) {
        active = lo;
    }
    if (active > loaded) {
        active = loaded;
    }
    UncheckedField<uint8_t>(info, kInfoLoaded) = loaded;
    UncheckedField<uint8_t>(info, kInfoActive) = active;
}
