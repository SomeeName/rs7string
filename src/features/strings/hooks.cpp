// The owners of the native GuitarStrings (strings.h "Owners"): the GuitarStrings and InstrumentInfo the guitar
// allocates (GSC1, GIA1, GIZ1), the registry of native InstrumentInfos, and the checks other code asks.
#include "features/strings/hooks.h"
#include "features/allocstats/allocstats.h"
#include "features/headstock/headstock.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include <cstring>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_native_strings = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_attach = 0;
__declspec(dllexport) float g_rs7_ns_rgb[2 * 2 * 3 * 3] = {};
__declspec(dllexport) float g_rs7_ns_v[2] = {0.4f, 0.4f};
}

using namespace rs7gs;

rs7gs::State rs7gs::g_strings = {};

namespace {
bool InfoAdd(uint8_t* info) {
    for (auto& slot : g_strings.infos) {
        if (_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&slot), info, nullptr) == nullptr) {
            return true;
        }
    }

    return false;
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

extern "C" void __cdecl Rs7Gs_InfoAllocated(uint8_t* info) {
    if (!info) {
        return;
    }

    std::memset(info + kStockInfoSize, 0, kInfoSize - kStockInfoSize);
    At<uint32_t>(info, kInfoMagicOff) = kInfoMagic;
    if (InfoAdd(info)) {
        Rs7Alloc_Inc(kAkInfos);
    } else if (Upto(&g_strings.logNoInfo, 1)) {
        Rs7Log("gsnative: InstrumentInfo registry full (%p stays 6-string)", info);
    }
}

extern "C" uint32_t __cdecl Rs7Gs_InfoFreeSize(uint8_t* info) {
    if (info && InfoRemove(info)) {
        Rs7Alloc_Dec(kAkInfos);
        At<uint32_t>(info, kInfoMagicOff) = 0;
        return kInfoSize;
    }

    // A native-sized info the registry did not take (full): its magic still tells.
    return kStockInfoSize;
}

extern "C" int __cdecl Rs7Gs_InfoNative(const uint8_t* info) {
    return InfoHas(info) && Rd<uint32_t>(info, kInfoMagicOff) == kInfoMagic;
}

// 0 = ready, else the first failing check (diagnostic, [debug] publish_trace): 1 strings not native, 2 strings
// 6/7 not allocated, 3 info not native, 4 guitar's info differs, 8 + (s - 6) * 6 + ghost * 3 + {0 no object,
// 1 no entity, 2 not loaded (+50h)}.
extern "C" int __cdecl Rs7Gs_ExtraWhy(uint8_t* guitar) {
    if (!guitar) {
        return 1;
    }

    uint8_t* gs = Rd<uint8_t*>(guitar, 0x0C);
    if (!IsNative(gs)) {
        return 1;
    }
    if (!Rd<uint8_t>(gs, kTail + 3)) {
        return 2;
    }

    uint8_t* info = Rd<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info)) {
        return 3;
    }
    if (Rd<uint8_t*>(guitar, 0x24) != info) {
        return 4;
    }

    for (int s = 6; s < 8; ++s) {
        for (int ghost = 0; ghost < 2; ++ghost) {
            uint8_t* obj = Child(gs, s, ghost != 0);
            const int at = 8 + (s - 6) * 6 + ghost * 3;
            if (!obj) {
                return at;
            }
            if (!Entity(obj)) {
                return at + 1;
            }
            if (!Rd<uint8_t>(obj, 0x50)) {
                return at + 2;
            }
        }
    }

    return 0;
}

extern "C" int __cdecl Rs7Gs_ExtraReady(uint8_t* guitar) {
    return Rs7Gs_ExtraWhy(guitar) == 0;
}

extern "C" uint8_t* __cdecl Rs7Gs_Construct(uint8_t* guitar, uint8_t* gs) {
    if (!gs) {
        return gs;
    }

    uint8_t* info = guitar ? Rd<uint8_t*>(guitar, 0x24) : nullptr;

    if (!InfoHas(info)) {
        info = nullptr;  // a stock-sized info: the loops stay stock (aliases)
    }
    uint8_t* em[2] = {};
    uint8_t* eg[2] = {};

    for (int i = 0; i < 2; ++i) {
        if ((em[i] = GameAlloc(0x110)) != nullptr) {
            Rs7Alloc_Inc(kAkStringObjs);
            GameStrCtor(em[i], 0);
        }

        if ((eg[i] = GameAlloc(0x110)) != nullptr) {
            Rs7Alloc_Inc(kAkStringObjs);
            GameStrCtor(eg[i], 1);
        }
    }

    Rs7Alloc_Inc(kAkGuitarStrings);
    InitTail(gs, guitar, info, em, eg);

    if (info) {
        At<uint8_t>(info, kInfoActive) = 0;
        RefreshCounts(info, g_rs7_s7_on != 0);
    }

    _InterlockedIncrement(&g_strings.native);
    Rs7_S7Record(gs);  // N5's per-guitar visibility state (string7.h)
    if (Upto(&g_strings.logCtor, 6)) {
        Rs7Log("gsnative: guitar %p strings %p native (capacity 8, strings 6/7 %p %p, ghosts %p %p, info %p%s)", guitar,
               gs, em[0], em[1], eg[0], eg[1], info, info ? "" : " NOT native: stock loops");
    }

    return gs;
}

extern "C" uint32_t __cdecl Rs7Gs_MeshCount(uint8_t* gs) {
    if (!g_rs7_native_strings || !IsNative(gs)) {
        return kStock;
    }

    uint8_t* info = Rd<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info) || !Rd<uint8_t>(gs, kTail + 3)) {
        return kStock;
    }

    const uint32_t n = Rd<uint8_t>(info, kInfoLoaded);

    return n > uint32_t(kStock) && n <= uint32_t(kCapacity) ? n : uint32_t(kStock);
}

extern "C" uint8_t* __cdecl Rs7Gs_SegHandle(uint8_t* guitar, uint32_t s) {
    return s == 6 ? Rs7Asset_Seg7Colour(guitar) : nullptr;
}

void Rs7Gs_Stats(Rs7GsStats* out) {
    out->native = int(g_strings.native);
    out->active7 = int(g_strings.active7);
    out->raised = int(g_strings.raised);
    out->lowered = int(g_strings.lowered);
}
