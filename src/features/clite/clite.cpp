// C-lite string-count logic (features/clite/clite.h): values in, values out. Game glue: hooks.cpp; log-only
// rebuild timing thread: watcher.cpp.
#include "features/clite/clite.h"
#include "features/clite/clite_internal.h"
#include "model/slots.h"
#include "platform/seh.h"
#include <windows.h>

uintptr_t g_rs7_clite = 0;
uintptr_t g_rs7_clite_desired[2] = {6, 6};
uintptr_t g_rs7_clite_bass[2] = {4, 4};
uintptr_t g_rs7_clite_mgr = 0;

unsigned rs7clite::Desired(bool has6, bool has7) {
    return !has6 ? 6u : has7 ? 8u : 7u;
}

unsigned rs7clite::BassDesired(bool has4, bool has5) {
    return !has4 ? 4u : has5 ? 6u : 5u;
}

unsigned rs7clite::BassCountFor(unsigned desired, unsigned player) {
    return player <= 1 && rs7::IsExtendedBass(desired) ? desired : 4u;
}

unsigned rs7clite::PegCount(unsigned n, bool nativePegset) {
    return n >= 7 ? n : nativePegset ? 7u : 6u;
}

unsigned rs7clite::BuildCountFor(unsigned desired, unsigned player) {
    return player <= 1 && rs7::IsExtendedGuitar(desired) ? desired : 6u;
}

bool rs7clite::NormaliseSlot1(uint32_t ret, unsigned slot1Count) {
    return ModeEntry(ret) != nullptr && rs7::IsExtendedGuitar(slot1Count);
}

bool rs7clite::AllowedCaller(uint32_t ret) {
    return ret == kRetSetFromManifest;
}

const char* rs7clite::ModeEntry(uint32_t ret) {
    for (const ModeEntryRet& e : kRetModeEntries) {
        if (e.ret == ret) {
            return e.name;
        }
    }

    return nullptr;
}

bool rs7clite::TunerMenu(uint32_t ret) {
    for (uint32_t r : kRetTunerMenu) {
        if (r == ret) {
            return true;
        }
    }

    return false;
}

unsigned rs7clite::RebuildTarget(uint32_t ret, uint32_t player, unsigned want, unsigned was, bool tunerLive,
                                 bool songLive, unsigned stock) {
    if (player > 1 || tunerLive) {
        return 0;
    }
    if (AllowedCaller(ret)) {
        return want != was ? want : 0u;
    }
    if (TunerMenu(ret)) {
        return !songLive && want != was ? want : 0u;
    }
    if (ModeEntry(ret) && want == stock && was != stock) {
        return stock;
    }

    return 0;
}

unsigned rs7clite::TunersHolding(uintptr_t mgr, uintptr_t* guitar) {
    if (!mgr) {
        return 0;
    }

    unsigned n = 0;

    __try {
        for (unsigned i = 0; i < kTunerSlotCount; ++i) {
            const uintptr_t t = *reinterpret_cast<const uintptr_t*>(mgr + kTunerMgrSlots + i * 4);
            const uintptr_t g = t ? *reinterpret_cast<const uintptr_t*>(t + kTunerGuitar) : 0;
            if (!g) {
                continue;
            }

            if (!n && guitar) {
                *guitar = g;
            }
            ++n;
        }
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return n ? n : 1u;  // unreadable: treat as held (no rebuild is the safe side)
    }

    return n;
}

using rs7clite::internal::UncheckedRead;

unsigned rs7clite::BuildCountOf(uintptr_t mgr, unsigned player) {
    if (!mgr || player > 1) {
        return 6;
    }

    __try {
        const uintptr_t holder = UncheckedRead<uintptr_t>(mgr + kMgrSlots + player * 4);
        const uintptr_t prof = holder ? UncheckedRead<uintptr_t>(holder + kHolderProfile) : 0;
        if (!prof || UncheckedRead<int>(prof + kProfileType) != 1) {
            return 6;
        }

        const unsigned n = UncheckedRead<uint8_t>(prof + kProfileCount);

        return rs7::GuitarStrings(n);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return 6;
    }
}
