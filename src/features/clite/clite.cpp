// C-lite string-count logic (features/clite/clite.h): values in, values out. Game glue: hooks.cpp; log-only
// rebuild timing thread: watcher.cpp.
#include "features/clite/clite.h"
#include "features/clite/clite_internal.h"
#include "model/slots.h"
#include "platform/seh.h"
#include <windows.h>

uintptr_t g_rsxr_clite = 0;
uintptr_t g_rsxr_clite_desired[2] = {6, 6};
uintptr_t g_rsxr_clite_bass[2] = {4, 4};
uintptr_t g_rsxr_clite_mgr = 0;

unsigned rsxrclite::Desired(bool has6, bool has7) {
    return !has6 ? 6u : has7 ? 8u : 7u;
}

unsigned rsxrclite::BassDesired(bool has4, bool has5) {
    return !has4 ? 4u : has5 ? 6u : 5u;
}

unsigned rsxrclite::BassCountFor(unsigned desired, unsigned player) {
    return player <= 1 && rsxr::IsExtendedBass(desired) ? desired : 4u;
}

unsigned rsxrclite::PegCount(unsigned n, bool nativePegset) {
    return n >= 7 ? n : nativePegset ? 7u : 6u;
}

unsigned rsxrclite::BuildCountFor(unsigned desired, unsigned player) {
    return player <= 1 && rsxr::IsExtendedGuitar(desired) ? desired : 6u;
}

bool rsxrclite::NormaliseSlot1(uint32_t ret, unsigned slot1Count) {
    return ModeEntry(ret) != nullptr && rsxr::IsExtendedGuitar(slot1Count);
}

bool rsxrclite::AllowedCaller(uint32_t ret) {
    return ret == kRetSetFromManifest;
}

const char* rsxrclite::ModeEntry(uint32_t ret) {
    for (const ModeEntryRet& e : kRetModeEntries) {
        if (e.ret == ret) {
            return e.name;
        }
    }

    return nullptr;
}

bool rsxrclite::TunerMenu(uint32_t ret) {
    for (uint32_t r : kRetTunerMenu) {
        if (r == ret) {
            return true;
        }
    }

    return false;
}

unsigned rsxrclite::RebuildTarget(uint32_t ret, uint32_t player, unsigned want, unsigned was, bool tunerLive,
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

unsigned rsxrclite::TunersHolding(uintptr_t mgr, uintptr_t* guitar) {
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
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return n ? n : 1u;  // unreadable: treat as held (no rebuild is the safe side)
    }

    return n;
}

using rsxrclite::internal::UncheckedRead;

unsigned rsxrclite::BuildCountOf(uintptr_t mgr, unsigned player) {
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

        return rsxr::GuitarStrings(n);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 6;
    }
}
