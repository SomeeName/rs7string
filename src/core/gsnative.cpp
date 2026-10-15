#include "core/gsnative.h"
#include "core/allocstats.h"
#include "core/assetload.h"
#include "core/colournative.h"
#include "core/fncopy.h"
#include "core/fncopy_specs.generated.h"
#include "core/headstock.h"
#include "core/publish.h"
#include "core/string7.h"
#include "core/tuner.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <intrin.h>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_native_strings = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_attach = 0;
__declspec(dllexport) float g_rs7_ns_rgb[2 * 2 * 3 * 3] = {};
__declspec(dllexport) float g_rs7_ns_v[2] = {0.4f, 0.4f};
extern uintptr_t g_rs7_target_GSC1;  // the stock ctor 0x599CE0 (stdcall(gs) ret 4)
}

// The copy / trampoline of every copied function (read by its entry stub, stubs_gs.cpp).
#define RS7_FNVARS(id)                                                                                                 \
    extern "C" __declspec(dllexport) uintptr_t g_rs7_fncopy_##id = 0;                                                  \
    extern "C" __declspec(dllexport) uintptr_t g_rs7_fntramp_##id = 0;
RS7_FNVARS(GSFR)
RS7_FNVARS(GSBD)
RS7_FNVARS(GSUL)
RS7_FNVARS(GSLD)
RS7_FNVARS(GSFO)
RS7_FNVARS(GSFI)
RS7_FNVARS(GSXF)
RS7_FNVARS(GSWD)
RS7_FNVARS(GSUP)
RS7_FNVARS(GSRS)
RS7_FNVARS(GSPK)
RS7_FNVARS(GSVB)
RS7_FNVARS(GSIC)
RS7_FNVARS(GSDM)
RS7_FNVARS(GSGL)
RS7_FNVARS(GSHC)
#undef RS7_FNVARS

using namespace rs7gs;

namespace {
template <class T>
T& At(uint8_t* p, uint32_t off) {
    return *reinterpret_cast<T*>(p + off);
}

template <class T>
T Rd(const uint8_t* p, uint32_t off) {
    return *reinterpret_cast<const T*>(p + off);
}

struct FnVar {
    const char* id;
    uintptr_t* copy;
    uintptr_t* tramp;
};

#define RS7_FNV(id) {#id, &g_rs7_fncopy_##id, &g_rs7_fntramp_##id}
const FnVar kFnVars[] = {RS7_FNV(GSFR), RS7_FNV(GSBD), RS7_FNV(GSUL), RS7_FNV(GSLD), RS7_FNV(GSFO), RS7_FNV(GSFI),
                         RS7_FNV(GSXF), RS7_FNV(GSWD), RS7_FNV(GSUP), RS7_FNV(GSRS), RS7_FNV(GSPK), RS7_FNV(GSVB),
                         RS7_FNV(GSIC), RS7_FNV(GSDM), RS7_FNV(GSGL), RS7_FNV(GSHC)};
#undef RS7_FNV

// Native InstrumentInfos (allocated by GIA1 with kInfoSize): a fixed set, lock-free.
constexpr int kMaxInfos = 64;
uint8_t* volatile g_infos[kMaxInfos] = {};

bool InfoAdd(uint8_t* info) {
    for (auto& slot : g_infos) {
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

    for (auto* p : g_infos) {
        if (p == info) {
            return true;
        }
    }

    return false;
}

bool InfoRemove(uint8_t* info) {
    for (auto& slot : g_infos) {
        if (_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&slot), nullptr, info) == info) {
            return true;
        }
    }

    return false;
}

volatile long g_native = 0, g_active7 = 0, g_raised = 0, g_lowered = 0;
volatile long g_logCtor = 0, g_logNoInfo = 0, g_logUse[kGsKinds] = {}, g_logRaise = 0, g_logLower = 0, g_logAttach = 0,
              g_logShape = 0, g_logLoad = 0, g_logReject[kGsKinds] = {};

bool Upto(volatile long* c, long cap) {
    return _InterlockedIncrement(c) <= cap;
}

// ---- game calls ----
uint8_t* GameAlloc(uint32_t size) {
    using Fn = uint8_t*(__cdecl*)(uint32_t, uint32_t);

    return reinterpret_cast<Fn>(g_rs7_fn_alloc)(size, 0);
}

void GameStrCtor(uint8_t* obj, uint32_t kind) {  // Rnd_GuitarString_Ctor(ESI = this, EAX = kind)
    __asm {
        push ebx
        push esi
        push edi
        mov esi, obj
        mov eax, kind
        call dword ptr [g_rs7_fn_strctor]
        pop edi
        pop esi
        pop ebx
    }
}

void GameShow(uint8_t* entity, uint32_t show) {  // EAX = entity, cdecl(show)
    __asm {
        push ebx
        push esi
        push edi
        push show
        mov eax, entity
        call dword ptr [g_rs7_fn_show]
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
}

void GameDetach(uint8_t* entity, uint32_t detach) {
    using Fn = uint32_t(__cdecl*)(uint8_t*, uint32_t, uint32_t);

    reinterpret_cast<Fn>(g_rs7_fn_detach)(entity, detach, 1);
}

// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 (EAX entity; mgr, seconds, 0, 1) ret 10h.
void GameFade(uint8_t* entity, bool in, float seconds) {
    const uintptr_t fn = in ? g_rs7_fn_fadein : g_rs7_fn_fadeout;
    const uint32_t mgr = g_rs7_fade_mgr ? *reinterpret_cast<const uint32_t*>(g_rs7_fade_mgr) : 0;
    if (!fn || !mgr || !entity) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push 3F800000h
        push 0
        push seconds
        push mgr
        mov eax, entity
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

// Rnd_GetStringAttachPoint 0x594530: ECX = info, cdecl(s, &vec3) -> AL.
bool GameAttach(uint8_t* info, uint32_t s, float* outVec) {
    uint32_t ok = 0;

    __asm {
        push ebx
        push esi
        push edi
        push outVec
        push s
        mov ecx, info
        call dword ptr [g_rs7_fn_attach]
        add esp, 8
        movzx eax, al
        mov ok, eax
        pop edi
        pop esi
        pop ebx
    }

    return ok != 0;
}

uint8_t* Entity(uint8_t* obj) {
    return obj ? Rd<uint8_t*>(obj, 0x04) : nullptr;
}

// [mode][string - 6][state][rgb]
float* Rgb(int mode, int s, int state) {
    return g_rs7_ns_rgb + ((mode * 2 + (s - 6)) * 3 + state) * 3;
}

void FillPalette() {
    if (g_rs7_native_colours) {
        g_rs7_ns_v[0] = g_rs7_ns_v[1] = g_rs7_s7_v;
        return;  // RGB lookups use the native matrix; no per-query extra palette.
    }

    const uintptr_t ref = g_rs7_colormgr;
    const uint8_t* mgr = ref ? *reinterpret_cast<uint8_t* const*>(ref) : nullptr;

    for (int mode = 0; mode < 2; ++mode) {
        // the stock dimmed value: state 2's value (max channel) of strings 0-5 (Rs7S7_DimColour)
        float v = rs7s7::kDefaultDim, sum = 0.0f;
        int n = 0;

        for (int s = 0; mgr && s < 6; ++s) {
            const float* c = *reinterpret_cast<const float* const*>(
                mgr + 0x350 + (mode * 28 * rs7colour::Width() + 2 * rs7colour::Width() + s) * 4);
            if (!c) {
                continue;
            }

            sum += c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            ++n;
        }

        if (n && sum > 0.0f) {
            v = sum / float(n);
        }
        const float* lit = g_rs7_fx_rgb + mode * 3;

        for (int s = 6; s < 8; ++s) {
            std::memcpy(Rgb(mode, s, 0), lit, 3 * sizeof(float));
            Rs7S7_DimColour(lit, v, Rgb(mode, s, 1));
            Rs7S7_GlowColour(lit, Rgb(mode, s, 2));
        }
    }

    g_rs7_ns_v[0] = g_rs7_ns_v[1] = g_rs7_s7_v;
}
}  // namespace

// ---- pure layout ----
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

// ---- stubs' C side ----
extern "C" void __cdecl Rs7Gs_InfoAllocated(uint8_t* info) {
    if (!info) {
        return;
    }

    std::memset(info + kStockInfoSize, 0, kInfoSize - kStockInfoSize);
    At<uint32_t>(info, kInfoMagicOff) = kInfoMagic;
    if (InfoAdd(info)) {
        Rs7Alloc_Inc(kAkInfos);
    } else if (Upto(&g_logNoInfo, 1)) {
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
    using Ctor = uint8_t*(__stdcall*)(uint8_t*);

    gs = reinterpret_cast<Ctor>(g_rs7_target_GSC1)(gs);
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

    _InterlockedIncrement(&g_native);
    Rs7_S7Record(gs);  // N5's per-guitar visibility state (string7.h)
    if (Upto(&g_logCtor, 6)) {
        Rs7Log("gsnative: guitar %p strings %p native (capacity 8, strings 6/7 %p %p, ghosts %p %p, info %p%s)", guitar,
               gs, em[0], em[1], eg[0], eg[1], info, info ? "" : " NOT native: stock loops");
    }

    return gs;
}

extern "C" int __cdecl Rs7Gs_Use(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    if (!g_rs7_native_strings || kind >= kGsKinds) {
        return 0;
    }

    uint8_t* gs = reg;

    __try {
        if (kind == kGsDim) {  // [esp+4] = session context: +0Ch -> +37Ch guitar -> +0Ch strings
            uint8_t* p = a1 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x0C) : nullptr;
            uint8_t* g = p ? Rd<uint8_t*>(p, 0x37C) : nullptr;

            gs = g ? Rd<uint8_t*>(g, 0x0C) : nullptr;
        } else if (kind == kGsGlow) {  // [esp+4] = StringRenderer: +4 strings
            gs = a1 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x04) : nullptr;
        } else if (kind == kGsHeadCol) {  // (headstock, guitar)
            gs = a2 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a2), 0x0C) : nullptr;
        }

        if (!IsNative(gs)) {
            return 0;
        }

        if (kind == kGsFree) {  // releases all 8 pairs whatever the info
            Rs7_S7Free(gs);     // N5's visibility entry (no 7th string object exists here)

            // The copy frees every non-null slot of the 8 pairs; the sized free follows (GSZ1) and clears
            // the marker, so count it here, where the marker is still set.
            for (int s = kStock; s < kCapacity; ++s) {
                if (Rd<uint8_t*>(gs, kMain + 4 * s)) {
                    Rs7Alloc_Dec(kAkStringObjs);
                }
                if (Rd<uint8_t*>(gs, kGhost + 4 * s)) {
                    Rs7Alloc_Dec(kAkStringObjs);
                }
            }

            Rs7Alloc_Dec(kAkGuitarStrings);
            return 1;
        }

        uint8_t* info = Rd<uint8_t*>(gs, kInfo);
        auto reject = [&](const char* why) {
            if (Upto(&g_logReject[kind], 1)) {
                Rs7Log("gsnative: kind %u on strings %p runs stock: %s", kind, gs, why);
            }

            return 0;
        };
        if (!Rs7Gs_InfoNative(info)) {
            return reject("its InstrumentInfo is not native");
        }
        if (!Rd<uint8_t>(gs, kTail + 3)) {
            return reject("its strings 6/7 could not be allocated");
        }

        uint8_t* owner = Rd<uint8_t*>(gs, 0);
        if (owner && Rd<uint8_t*>(owner, 0x24) != info) {
            return reject("owner +24h is not its InstrumentInfo");
        }

        if (kind == kGsLoad) {  // the load reads the info from (context +4) +24h
            uint8_t* ctx = reinterpret_cast<uint8_t*>(a2);
            uint8_t* g = ctx ? Rd<uint8_t*>(ctx, 0x04) : nullptr;
            if (!g || Rd<uint8_t*>(g, 0x24) != info) {
                return reject("load context's info differs");
            }

            At<uint8_t>(gs, kTail + 1) += 1;
        } else if (kind == kGsInitCol) {
            if (reinterpret_cast<uint8_t*>(a2) != info) {
                return reject("InitColors info differs");
            }

            FillPalette();
        } else if (kind == kGsHeadCol) {
            if (Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a2), 0x24) != info) {
                return reject("headstock guitar's info differs");
            }
        }

        RefreshCounts(info, g_rs7_s7_on != 0);
        if (Upto(&g_logUse[kind], 1)) {
            Rs7Log("gsnative: first native call kind %u (strings %p, loaded %u, active %u)", kind, gs,
                   Rd<uint8_t>(info, kInfoLoaded), Rd<uint8_t>(info, kInfoActive));
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
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

extern "C" int __cdecl Rs7Gs_AttachExt(uint8_t* info, uint32_t s, float* out) {
    s &= 0xFF;
    float a0[3], a1[3];

    std::memcpy(a0, out, sizeof a0);
    std::memcpy(a1, out, sizeof a1);
    if (!GameAttach(info, 0, a0) || !GameAttach(info, 1, a1)) {
        return 0;
    }

    ExtrapolateAttach(a0, a1, s, out);  // s 6: 2*a0 - a1, s 7: 3*a0 - 2*a1
    if (Upto(&g_logAttach, 2)) {
        Rs7Log("gsnative: string %u attach (%.3f, %.3f, %.3f) from strings 0/1", s, double(out[0]), double(out[1]),
               double(out[2]));
    }

    return 1;
}

extern "C" void __cdecl Rs7Gs_ShapeDone(uint8_t* obj, uint32_t s) {
    s &= 0xFF;
    if (!obj || !Rd<uint8_t>(obj, 0x50) || s < 6 || s > 7) {
        return;
    }

    if (uint8_t* h = Rd<uint8_t*>(obj, 0x3C)) {
        At<float>(h, 0x14) = g_rs7_ns_v[s - 6];
    }
    if (Upto(&g_logShape, 2)) {
        Rs7Log("gsnative: string %u (%s %p) shader params found, atlas row %.2f", s,
               Rd<uint32_t>(obj, 0) ? "ghost" : "main", obj, double(g_rs7_ns_v[s - 6]));
    }
}

// ---- per frame ----
uint8_t* Rs7Gs_Active(uint8_t* gs, int s, bool ghost) {
    if (!g_rs7_native_strings || !IsNative(gs) || s < kStock || s >= kCapacity) {
        return nullptr;
    }

    uint8_t* info = Rd<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info) || Rd<uint8_t>(info, kInfoActive) <= s) {
        return nullptr;
    }

    uint8_t* obj = Child(gs, s, ghost);
    if (!obj || !Entity(obj) || !Rd<uint8_t>(obj, 0x50)) {
        return nullptr;
    }

    return obj;
}

bool Rs7Gs_Frame(uint8_t* guitar, uint8_t* gs, bool want) {
    if (!g_rs7_native_strings || !IsNative(gs)) {
        return false;
    }

    uint8_t* info = Rd<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info) || !Rd<uint8_t>(gs, kTail + 3)) {
        return true;
    }

    RefreshCounts(info, g_rs7_s7_on != 0);

    if (g_rs7_native_publish) {
        // C-lite: the guitar shows the count it was built with (RefreshCounts: 4 bass, 6, 7 or 8); this only
        // shows or hides the added string objects accordingly (the first frame after a load starts from the
        // stock width, so the added strings fade in with the stock ones).
        const uint8_t now = Rd<uint8_t>(info, kInfoActive);
        uint8_t& shown = At<uint8_t>(gs, kTail);
        const uint8_t before = shown ? shown : now == 4 ? 4 : 6;  // first frame: the stock width
        const bool loaded = Rd<uint8_t>(gs, kTail + 2) != Rd<uint8_t>(gs, kTail + 1);

        if (now != before && Upto(&g_logRaise, 6)) {
            Rs7Log("gsnative: guitar %p shows %u strings (was %u)", guitar, now, before);
        }

        for (int s = 6; s < 8; ++s) {
            uint8_t* main = Child(gs, s, false);
            uint8_t* ghost = Child(gs, s, true);

            if (s < now) {
                if (s >= before || loaded) {
                    if (uint8_t* e = Entity(main)) {
                        GameShow(e, 1);
                        if (Rd<uint8_t>(gs, 0x39)) {
                            GameFade(e, true, rs7head::kRaiseFadeIn);
                        } else if (Rd<uint8_t>(gs, 0x38)) {
                            GameFade(e, false, 0.05f);
                        }
                    }
                }
            } else if (s < before || loaded || !shown) {
                if (uint8_t* e = Entity(main)) {
                    GameShow(e, 0);
                }

                if (uint8_t* e = Entity(ghost)) {
                    GameShow(e, 0);
                    GameDetach(e, 1);
                }
            }
        }

        shown = now;
        At<uint8_t>(gs, kTail + 2) = Rd<uint8_t>(gs, kTail + 1);
        return true;
    }

    const uint8_t stock = Rd<uint8_t>(info, 0x0B);
    const uint8_t loaded = Rd<uint8_t>(info, kInfoLoaded);
    const uint8_t before = Rd<uint8_t>(info, kInfoActive);
    const uint8_t lo = stock < loaded ? stock : loaded;
    // Lowered only after kLowerFrames frames without the 7th string, as the native PegSet does: a
    // tuner -> song hand-over drops the tuner's mark a few frames before S4 marks the song.
    uint8_t& unwanted = At<uint8_t>(gs, kTail + 0);

    if (want) {
        unwanted = 0;
    } else if (unwanted < kLowerFrames) {
        ++unwanted;
    }
    const bool keep = !want && before > lo && unwanted < kLowerFrames;
    const uint8_t visible = g_rs7_publish_ports && loaded > 7 ? 7 : loaded;
    const uint8_t now = keep ? before : want && loaded > kStock ? visible : lo;
    uint8_t* m6 = Child(gs, kStock, false);
    uint8_t* g6 = Child(gs, kStock, true);

    if (now > before) {
        At<uint8_t>(info, kInfoActive) = now;
        _InterlockedIncrement(&g_raised);
        _InterlockedIncrement(&g_active7);

        // Shown again; its fade state = the strings' (+39h faded in, +38h faded out, else still
        // un-faded like them: the coming stock FadeIn covers it, it is in the active count now).
        if (uint8_t* e = Entity(m6)) {
            GameShow(e, 1);
            if (Rd<uint8_t>(gs, 0x39)) {
                GameFade(e, true, rs7head::kRaiseFadeIn);
            } else if (Rd<uint8_t>(gs, 0x38)) {
                GameFade(e, false, 0.05f);
            }
        }

        if (Upto(&g_logRaise, 6)) {
            Rs7Log("gsnative: guitar %p shows the 7th string (active %u -> %u; strings faded in %u, out %u)", guitar,
                   before, now, Rd<uint8_t>(gs, 0x39), Rd<uint8_t>(gs, 0x38));
        }
    } else if (now < before) {
        At<uint8_t>(info, kInfoActive) = now;
        _InterlockedIncrement(&g_lowered);
        _InterlockedDecrement(&g_active7);
        if (uint8_t* e = Entity(m6)) {
            GameShow(e, 0);
        }

        if (uint8_t* e = Entity(g6)) {
            GameShow(e, 0);
            GameDetach(e, 1);
        }

        if (Upto(&g_logLower, 6)) {
            Rs7Log("gsnative: guitar %p hides the 7th string (active %u -> %u)", guitar, before, now);
        }
    }

    // Inactive: the load's take un-culled string 6 (0x595B40); cull it after every load.
    if (now <= kStock && Rd<uint8_t>(gs, kTail + 2) != Rd<uint8_t>(gs, kTail + 1)) {
        At<uint8_t>(gs, kTail + 2) = Rd<uint8_t>(gs, kTail + 1);
        if (uint8_t* e = Entity(m6)) {
            GameShow(e, 0);
        }
        if (Upto(&g_logLoad, 4)) {
            Rs7Log("gsnative: guitar %p loaded, 7th string culled (not shown)", guitar);
        }
    }

    return true;
}

bool Rs7Gs_BuildCopies() {
    for (int i = 0; i < kRs7FnSpecCount; ++i) {
        const Rs7FnSpec& sp = kRs7FnSpecs[i];
        const FnVar* v = nullptr;

        for (const FnVar& x : kFnVars) {
            if (std::strcmp(x.id, sp.id) == 0) {
                v = &x;
            }
        }

        char why[160] = {};
        uintptr_t c = 0, t = 0;

        if (!v || !Rs7Fn_Build(sp, sp.va, &c, &t, why, sizeof why)) {
            Rs7Log("gsnative: copy %s failed (%s) - native strings off", sp.id, v ? why : "no variables");

            for (const FnVar& x : kFnVars) {
                *x.copy = *x.tramp = 0;
            }

            return false;
        }

        *v->copy = c;
        *v->tramp = t;
    }

    FillPalette();
    Rs7Log("gsnative: %d function copies built (native N-slot GuitarStrings)", kRs7FnSpecCount);

    return true;
}

void Rs7Gs_Stats(Rs7GsStats* out) {
    out->native = int(g_native);
    out->active7 = int(g_active7);
    out->raised = int(g_raised);
    out->lowered = int(g_lowered);
}
