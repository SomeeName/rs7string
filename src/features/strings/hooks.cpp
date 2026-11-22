// The owners of the native GuitarStrings (strings.h "Owners"): the GuitarStrings and InstrumentInfo the guitar
// allocates (GSC1, GIA1, GIZ1), the registry of native InstrumentInfos, and the checks other code asks.
#include "features/strings/hooks.h"
#include "features/allocstats/allocstats.h"
#include "features/headstock/headstock.h"
#include "features/string7/string7.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <cstring>

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_native_strings = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_attach = 0;
__declspec(dllexport) float g_rsxr_ns_rgb[2 * 2 * 3 * 3] = {};
__declspec(dllexport) float g_rsxr_ns_v[2] = {0.4f, 0.4f};
}

using namespace rsxrgs;

rsxrgs::State rsxrgs::g_strings = {};

namespace {
uint32_t InfoAdd(uint8_t* info) {
    for (uint32_t i = 0; i < kMaxInfos; ++i) {
        auto& slot = g_strings.infos[i];
        if (_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&slot), info, nullptr) == nullptr) {
            return i + 1;
        }
    }

    return 0;
}

bool InfoHas(const uint8_t* info) {
    if (!info) {
        return false;
    }

    for (auto* p : g_strings.infos) {
        if (p == info) {
            return true;
        }
    }

    return false;
}

bool InfoRemove(uint8_t* info) {
    for (auto& slot : g_strings.infos) {
        if (_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&slot), nullptr, info) == info) {
            return true;
        }
    }

    return false;
}
}  // namespace

extern "C" void __cdecl RsxrGs_InfoAllocated(uint8_t* info) {
    if (!info) {
        return;
    }

    std::memset(info + kStockInfoSize, 0, kInfoSize - kStockInfoSize);
    UncheckedField<uint32_t>(info, kInfoMagicOff) = kInfoMagic;
    if (InfoHas(info) || InfoAdd(info)) {
        RsxrAlloc_Inc(kAkInfos);
    } else if (RsxrLog_Upto(&g_strings.logNoInfo, 1)) {
        RsxrLog("gsnative: InstrumentInfo registry full (%p stays 6-string)", info);
    }
}

uint32_t rsxrgs::ReserveInfo() {
    return InfoAdd(reinterpret_cast<uint8_t*>(uintptr_t(1)));
}

void rsxrgs::FinishInfo(uint32_t token, uint8_t* info) {
    if (!token || token > kMaxInfos) {
        return;
    }

    auto& slot = g_strings.infos[token - 1];

    _InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slot), info);
    if (info) {
        RsxrGs_InfoAllocated(info);
    }
}

extern "C" uint32_t __cdecl RsxrGs_InfoFreeSize(uint8_t* info) {
    if (info && InfoRemove(info)) {
        RsxrAlloc_Dec(kAkInfos);
        UncheckedField<uint32_t>(info, kInfoMagicOff) = 0;
        return kInfoSize;
    }

    // GIA1 requests the stock size when no registry slot can be reserved.
    return kStockInfoSize;
}

extern "C" int __cdecl RsxrGs_InfoNative(const uint8_t* info) {
    return InfoHas(info) && UncheckedRead<uint32_t>(info, kInfoMagicOff) == kInfoMagic;
}

// 0 = ready, else the first failing check (diagnostic, [debug] publish_trace): 1 strings not native, 2 strings
// 6/7 not allocated, 3 info not native, 4 guitar's info differs, 8 + (s - 6) * 6 + ghost * 3 + {0 no object,
// 1 no entity, 2 not loaded (+50h)}.
extern "C" int __cdecl RsxrGs_ExtraWhy(uint8_t* guitar) {
    if (!guitar) {
        return 1;
    }

    uint8_t* gs = UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarStrings);
    if (!IsNative(gs)) {
        return 1;
    }

    uint8_t* info = UncheckedRead<uint8_t*>(gs, kInfo);
    if (!RsxrGs_InfoNative(info)) {
        return 3;
    }
    if (UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarInfo) != info) {
        return 4;
    }

    const int loaded = UncheckedRead<uint8_t>(info, kInfoLoaded);
    if (loaded > kCapacity || (loaded > kStock && !UncheckedRead<uint8_t>(gs, kTail + 3))) {
        return 2;
    }

    for (int s = kStock; s < loaded; ++s) {
        for (int ghost = 0; ghost < 2; ++ghost) {
            uint8_t* obj = Child(gs, s, ghost != 0);
            const int at = 8 + (s - 6) * 6 + ghost * 3;
            if (!obj) {
                return at;
            }
            if (!Entity(obj)) {
                return at + 1;
            }
            if (!UncheckedRead<uint8_t>(obj, 0x50)) {
                return at + 2;
            }
        }
    }

    return 0;
}

extern "C" int __cdecl RsxrGs_ExtraReady(uint8_t* guitar) {
    return RsxrGs_ExtraWhy(guitar) == 0;
}

void rsxrgs::RefreshBuildCounts(uint8_t* gs, uint8_t* info) {
    if (!info) {
        return;
    }

    const bool native = RsxrGs_InfoNative(info) != 0;

    if (!native || !IsNative(gs) || Rsxr_SafeRead<uint8_t*>(gs, kInfo) != info ||
        !Rsxr_SafeRead<uint8_t>(gs, kTail + 3)) {
        // FromProfile writes +0Bh after GSC1. Stock loops need the cap again before their first consumer.
        if (Rsxr_SafeRead<uint8_t>(info, rsxr::kInfoStrings) > kStock) {
            UncheckedField<uint8_t>(info, rsxr::kInfoStrings) = kStock;
        }
        if (native) {
            UncheckedField<uint8_t>(info, kInfoActive) = UncheckedField<uint8_t>(info, kInfoLoaded) =
                Rsxr_SafeRead<uint8_t>(info, rsxr::kInfoStrings);
        }
    } else {
        UncheckedField<uint8_t>(info, kInfoActive) = 0;
        RefreshCounts(info, g_rsxr_s7_on != 0);
    }
}

extern "C" uint8_t* __cdecl RsxrGs_Construct(uint8_t* guitar, uint8_t* gs) {
    if (!gs) {
        return gs;
    }

    uint8_t* info = guitar ? UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarInfo) : nullptr;

    if (!InfoHas(info)) {
        info = nullptr;  // a stock-sized info: the loops stay stock (aliases)
    }
    uint8_t* em[2] = {};
    uint8_t* eg[2] = {};

    for (int i = 0; i < 2; ++i) {
        if ((em[i] = GameAlloc(0x110)) != nullptr) {
            RsxrAlloc_Inc(kAkStringObjs);
            GameStrCtor(em[i], 0);
        }

        if ((eg[i] = GameAlloc(0x110)) != nullptr) {
            RsxrAlloc_Inc(kAkStringObjs);
            GameStrCtor(eg[i], 1);
        }
    }

    RsxrAlloc_Inc(kAkGuitarStrings);
    InitTail(gs, guitar, info, em, eg);

    RefreshBuildCounts(gs, guitar ? UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarInfo) : nullptr);

    _InterlockedIncrement(&g_strings.native);
    Rsxr_S7Record(gs);  // N5's per-guitar visibility state (string7.h)
    if (RsxrLog_Upto(&g_strings.logCtor, 6)) {
        RsxrLog("gsnative: guitar %p strings %p native (capacity 8, strings 6/7 %p %p, ghosts %p %p, info %p%s)",
                guitar, gs, em[0], em[1], eg[0], eg[1], info, info ? "" : " NOT native: stock loops");
    }

    return gs;
}

extern "C" uint32_t __cdecl RsxrGs_MeshCount(uint8_t* gs) {
    if (!g_rsxr_native_strings || !IsNative(gs)) {
        return kStock;
    }

    uint8_t* info = UncheckedRead<uint8_t*>(gs, kInfo);
    if (!RsxrGs_InfoNative(info) || !UncheckedRead<uint8_t>(gs, kTail + 3)) {
        return kStock;
    }

    const uint32_t n = UncheckedRead<uint8_t>(info, kInfoLoaded);

    return n > uint32_t(kStock) && n <= uint32_t(kCapacity) ? n : uint32_t(kStock);
}

extern "C" uint8_t* __cdecl RsxrGs_SegHandle(uint8_t* guitar, uint32_t s) {
    return RsxrAsset_SegColour(guitar, s);
}

void RsxrGs_Stats(RsxrGsStats* out) {
    out->native = int(g_strings.native);
    out->active7 = int(g_strings.active7);
    out->raised = int(g_strings.raised);
    out->lowered = int(g_strings.lowered);
}
