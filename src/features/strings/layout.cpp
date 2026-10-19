// The native GuitarStrings / InstrumentInfo layout (strings.h): values in, values out.
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include <cstring>

using namespace rs7gs;

bool rs7gs::IsNative(const uint8_t* gs) {
    return gs && Rd<uint16_t>(gs, kMarkerOff) == kMarker && Rd<uint32_t>(gs, kMagicOff) == kMagic;
}

void rs7gs::InitTail(uint8_t* gs, uint8_t* guitar, uint8_t* info, uint8_t* const extraMain[2],
                     uint8_t* const extraGhost[2]) {
    for (int s = 0; s < kStock; ++s) {
        At<uint8_t*>(gs, kMain + 4 * s) = Rd<uint8_t*>(gs, 0x04 + 4 * s);
        At<uint8_t*>(gs, kGhost + 4 * s) = Rd<uint8_t*>(gs, 0x1C + 4 * s);
    }

    for (int i = 0; i < 2; ++i) {
        At<uint8_t*>(gs, kMain + 4 * (kStock + i)) = extraMain[i];
        At<uint8_t*>(gs, kGhost + 4 * (kStock + i)) = extraGhost[i];
    }

    std::memset(gs + kGlow, 0, 4 * kCapacity);
    At<uint8_t>(gs, kTail + 0) = 0;  // frames without the 7th string
    At<uint8_t>(gs, kTail + 1) = 0;  // load generation
    At<uint8_t>(gs, kTail + 2) = 0;  // culled generation
    bool complete = true;

    for (int i = 0; i < 2; ++i) {
        complete = complete && extraMain[i] && extraGhost[i];
    }

    At<uint8_t>(gs, kTail + 3) = complete ? 1 : 0;
    At<uint8_t*>(gs, kGuitar) = guitar;
    At<uint8_t*>(gs, kInfo) = info;
    At<uint16_t>(gs, kMarkerOff) = kMarker;
    At<uint32_t>(gs, kMagicOff) = kMagic;
}

void rs7gs::RefreshCounts(uint8_t* info, bool seventh) {
    const uint8_t stock = Rd<uint8_t>(info, 0x0B);

    if (g_rs7_publish_ports) {
        // C-lite: +B is the count the guitar was BUILT with (4 bass, 6, 7 or 8) and stays so; the
        // capacity of 8 string objects is preloaded for every guitar, the active count is the build count.
        At<uint8_t>(info, kInfoLoaded) = stock == 4 ? 4 : 8;
        At<uint8_t>(info, kInfoActive) = stock == 4 ? 4 : stock < 6 ? 6 : stock > 8 ? 8 : stock;
        // The native tail's mode[6] / mode[7] (+38h / +3Ch) would be a mode no stock string has when zero:
        // mirror string 0's (the BD1-BD4 hooks do the same; HYPOTHESIS: no unguarded reader found, this keeps
        // any such reader on a valid stock value). Once, when the guitar's info is first refreshed.
        if (stock != 4 && !Rd<uint32_t>(info, 0x38) && !Rd<uint32_t>(info, 0x3C)) {
            At<uint32_t>(info, 0x38) = At<uint32_t>(info, 0x3C) = Rd<uint32_t>(info, 0x20);
        }
        return;
    }

    uint8_t loaded = stock == kStock && seventh ? uint8_t(kStock + 1) : stock;

    if (loaded > kCapacity) {
        loaded = kCapacity;
    }
    uint8_t active = Rd<uint8_t>(info, kInfoActive);
    const uint8_t lo = stock < loaded ? stock : loaded;

    if (active < lo) {
        active = lo;
    }
    if (active > loaded) {
        active = loaded;
    }
    At<uint8_t>(info, kInfoLoaded) = loaded;
    At<uint8_t>(info, kInfoActive) = active;
}
