#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/headstock/assetload_head.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/state.h"
#include "features/string7/string7.h"
#include "model/groups.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// The six headstock string segments of the edited model and the 7th: shader params, colour, draw layer.

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_fn_strparams = 0;
}

using namespace rsxrasset;
using rsxrhead::g_head;

namespace {
const int kInfoGuitar[4] = {1, 0, 0, 0};  // Rnd_SetStringShaderParams only reads info+0 (2 = bass)
const int kInfoBass[4] = {rsxr::kKindBass, 0, 0, 0};
constexpr uint32_t kStrShapeFound = 0x50;
constexpr uint32_t kStrColour = 0x38;

// Rnd_SetStringShaderParams 0x5998F0 (EAX = param object, cdecl (info, slot, mesh); it releases the
// mesh once, so it is AddRef'd first, as 0x58E2BB..0x58E2C1 does): sets the segment's shader extra
// data and points the param object at it, so the game's per-frame segment colour reaches it.
bool BindSegment(void* params, uint8_t* mesh, uint32_t slot, bool bass = false) {
    if (!g_rsxr_fn_strparams || !mesh || !rsxrs7d::IsKindOf(mesh, "NiMesh")) {
        return false;
    }

    __try {
        AddRef(mesh);
        const int* info = bass ? kInfoBass : kInfoGuitar;

        __asm {
            push mesh
            push slot
            push info
            mov eax, params
            call dword ptr [g_rsxr_fn_strparams]
            add esp, 0Ch
        }

        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

// Param object fields (Rnd_SetStringShaderParams): 0 g_flickerPhase, 1 g_flickerFade,
// 2 g_sharpToBlurLerpVal, 3 g_diffuseColor (RGBA +14h..+23h), 4 v_positionString,
// 5 g_vertexWidthScale, 6 g_glowScale, 7 g_glowPow, 8 g_glowBrightness (floats at +14h).
uint32_t ValueSize(int field) {
    return field == 3 ? 16u : 4u;
}

void SwapRef(uintptr_t* slot, uintptr_t now) {  // NiPointer-style assignment, the old one stays alive
    const uintptr_t old = *slot;
    if (old == now || !now) {
        return;
    }

    AddRef(reinterpret_cast<void*>(now));
    *slot = now;
    if (old) {
        DropRef(reinterpret_cast<void*>(old));
    }
}

// The stock param object of segment s of the guitar's headstock (Rnd_Headstock +20h + s*4), nullptr without one.
uintptr_t* StockParams(uint8_t* head, int s) {
    return Rsxr_SafeRead<uintptr_t*>(head, kHeadParams + 4u * uint32_t(s));
}

// The bass string shader has the four stock rows (V of 0x5998F0's slot); the low B takes the E row, the high C the G
// row, like the guitar's added strings take string 0's.
uint32_t ShaderRow(const Head& h, int s) {
    return uint32_t(!h.bass ? s : rsxr::kBassNeighbour[s]);  // s < kSegs (6)
}

// Holder destructor 0x58DAB0: last slot first, decrement +4, vtable +4 when the last reference goes away.
void ReleaseHolder(uintptr_t* holder) {
    for (int i = 8; i >= 0; --i) {
        void* obj = reinterpret_cast<void*>(holder[i]);

        holder[i] = 0;
        if (!obj) {
            continue;
        }

        __try {
            if (_InterlockedDecrement(reinterpret_cast<volatile long*>(static_cast<uint8_t*>(obj) + kRefCount)) == 0) {
                using Fn = void(__fastcall*)(void*, void*);
                void* const* vt = *static_cast<void* const* const*>(obj);

                reinterpret_cast<Fn>(vt[1])(obj, nullptr);
            }
        } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        }
    }
}
}  // namespace

namespace rsxrasset {
void ReleaseParams(Head& h) {
    for (auto& holder : h.paramsX) {
        ReleaseHolder(holder);
    }

    for (auto& holder : h.altHold) {
        ReleaseHolder(holder);
    }

    for (auto& holder : h.stockHold) {
        ReleaseHolder(holder);
    }

    std::memset(h.segOk, 0, sizeof h.segOk);
    h.bound = false;
}

// Once per frame instance: our own param holders for the alt's segments (the stock holders are not
// touched yet) and a referenced copy of the stock holders' pointers.
void BindAlt(Head& h) {
    ReleaseParams(h);
    uint8_t* head = Rsxr_SafeRead<uint8_t*>(h.guitar, kGuitarHead);
    int n = 0;

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = StockParams(head, s);

        h.segOk[s] = false;
        if (!params || !h.altSeg[s]) {
            continue;
        }

        std::memset(h.altHold[s], 0, sizeof h.altHold[s]);
        if (!BindSegment(h.altHold[s], h.altSeg[s], ShaderRow(h, s), h.bass)) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            h.stockHold[s][i] = 0;
            uintptr_t p = 0;
            if (!Rsxr_SafeCopyZero(&p, params + i, sizeof p) || !p || !h.altHold[s][i]) {
                continue;
            }

            AddRef(reinterpret_cast<void*>(p));
            h.stockHold[s][i] = p;
        }

        h.segOk[s] = true;
        ++n;
    }

    int extra = 0;

    for (int e = 0; e < kExtra; ++e) {
        if (h.segX[e]) {
            BindSegment(h.paramsX[e], h.segX[e], 0);  // string 0's V row, like the added string lines
            ++extra;
        }
    }

    h.bound = true;
    RsxrAlloc_Inc(kAkAssetBinds);

    if (RsxrLog_Upto(&g_head.logBind, 8)) {
        RsxrLog("assetload: guitar %p: %d of %d headstock string segment(s) of the edited model bound; %d extra "
                "segment(s) (string_size_7/8) bound to their own params",
                h.guitar, n, kSegs, extra);
    }
}

// The game's per-frame segment state goes through the six stock param objects: point them at the
// alt's extra data (toAlt) or back, carrying the current values over, so nothing is reset.
void PointParams(Head& h, bool toAlt) {
    uint8_t* head = Rsxr_SafeRead<uint8_t*>(h.guitar, kGuitarHead);

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = StockParams(head, s);
        if (!params || !h.segOk[s]) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            const uintptr_t from = toAlt ? h.stockHold[s][i] : h.altHold[s][i];
            const uintptr_t to = toAlt ? h.altHold[s][i] : h.stockHold[s][i];

            if (!to) {  // back to a slot the game never bound (a bass's added string): empty again
                if (!toAlt && params[i] == from && from) {
                    params[i] = 0;
                    DropRef(reinterpret_cast<void*>(from));
                }

                continue;
            }

            uint8_t v[16];

            if (from && Rsxr_SafeCopyZero(v, reinterpret_cast<uint8_t*>(from) + kColourRgb, ValueSize(i))) {
                Rsxr_SafeStore(reinterpret_cast<uint8_t*>(to) + kColourRgb, v, ValueSize(i));
            }
            SwapRef(params + i, to);
        }

        // Rnd_Headstock_UpdateSegmentColours 0x58E4F0 colours slot s only when loaded (+18h + s). The frame's
        // loader ran once, at the stock string count, so a 5/6-string bass's added slots stay unloaded there.
        uint8_t* loaded = head + kHeadLoaded + uint32_t(s);

        if (toAlt) {
            h.loadedWas[s] = Rsxr_SafeRead<uint8_t>(head, kHeadLoaded + uint32_t(s));
            const uint8_t one = 1;

            Rsxr_SafeStore(loaded, &one, 1);
        } else {
            Rsxr_SafeStore(loaded, &h.loadedWas[s], 1);
        }
    }

    h.pointed = toAlt;
}

void FindSegments(Head& h) {
    // The stock model: the first NiNode sibling of rsxr_alt (culled or not, so the restore always finds it).
    uint8_t* stock = nullptr;

    EachChild(Rsxr_SafeRead<uint8_t*>(h.alt, kParent), [&](uint8_t* c) {
        if (c != h.alt && rsxrs7d::IsKindOf(c, "NiNode")) {
            stock = c;
        }

        return !stock;
    });

    // A bass binds slots 0..count-1 (Rnd_Headstock_LoadStringMeshes 0x58E090, CONFIRMED), so a 5/6-string bass fits
    // the six stock slots and has no extra segment; its names follow the string order (BassSegName). A guitar keeps
    // string_size_<6 - s>.
    const uint8_t* info = Rsxr_SafeRead<const uint8_t*>(h.guitar, kGuitarInfo);
    const int count = info ? Rsxr_SafeRead<uint8_t>(info, kInfoStrings) : 0;

    h.bass = info && rsxr::IsBassKind(Rsxr_SafeRead<uint8_t>(info, rsxr::kInfoKind));

    for (int s = 0; s < kSegs; ++s) {
        char name[24];

        std::snprintf(name, sizeof name, "string_size_%d", h.bass ? BassSegName(s) : kSegs - s);
        const bool used = !h.bass || s < count;

        h.altSeg[s] = used ? FindByName(h.alt, name) : nullptr;
        h.stockSeg[s] = used && stock ? FindByName(stock, name) : nullptr;
    }

    for (int e = 0; e < kExtra; ++e) {
        if (h.bass) {
            h.segX[e] = nullptr;
            continue;
        }

        char name[24];

        std::snprintf(name, sizeof name, "string_size_%d", kSegs + 1 + e);
        h.segX[e] = FindByName(h.alt, name);
    }
}

// An extra segment's colour, every shown frame, without native strings (with them the stock segment colour loop
// does it through RsxrAsset_SegColour): what Rnd_Headstock_UpdateSegmentColours 0x58E4F0 does for segment s <
// stringCount (string object +50h base shape found: copy the colour handle +38h's RGBA (+14h..+23h) onto the
// segment's g_diffuseColor), for the 7th string's object S7. So it is lit / dimmed exactly like the 7th string
// line (N11). Without S7 ([neck] string7_line=0) the static [color] string6 colour; the 8th segment (no
// non-native string object) the static 8th string colour.
void ColourExtra(Head& h, int e) {
    uint8_t* cd = reinterpret_cast<uint8_t*>(h.paramsX[e][3]);
    if (!cd) {
        return;
    }

    uint8_t* s7 = e == 0 ? RsxrS7_ObjectOf(h.guitar) : nullptr;
    uint8_t* src =
        (s7 && Rsxr_SafeRead<uint8_t>(s7, kStrShapeFound)) ? Rsxr_SafeRead<uint8_t*>(s7, kStrColour) : nullptr;
    uint8_t rgba[16];

    if (src && Rsxr_SafeCopyZero(rgba, src + kColourRgb, sizeof rgba)) {
        Rsxr_SafeStore(cd + kColourRgb, rgba, sizeof rgba);
        if (RsxrLog_Upto(&g_head.logSeg7, 2)) {
            RsxrLog("assetload: guitar %p: string_size_7 follows the 7th string's colour (S7 %p colour %p)", h.guitar,
                    s7, src);
        }
    } else if (e == 0) {
        Rsxr_SafeStore(cd + kColourRgb, g_rsxr_fx_rgb + 3 * RsxrColour_Mode(), 3 * sizeof(float));
    } else {
        float rgb[3];

        Rsxr_RgbToFloats(rsxrcolour::Colour8(RsxrColour_Mode()), rgb);
        Rsxr_SafeStore(cd + kColourRgb, rgb, sizeof rgb);
    }
}

// The 7th segment at the nut (issue #17 / #23 / nut gap, 2026-10-15). string_size_7 is left exactly as
// the model has it: its nut end equals string_size_1..6's (model y 15.21, same height and radius,
// pitch 1.608 in z) and lands on the 7th neck string's start (RsxrGs_AttachExt, features/strings/strings.h
// ExtrapolateAttach: z to 0.001, y to 0.011), like strings 0-5 meet theirs. An older workaround
// sheared the segment's nut end 0.30 deeper (x) on the idea that the 7th nut slot is lower; the real
// cause of "the segment draws in front of the nut" was the draw layer (LayerExtra below, CONFIRMED in game
// by issue #23), so the shear only left the thick segment end 0.30 off the neck string's axis, more
// than the end radius (0.20): the visible gap at the nut. It is gone.

// Issue #23: string_size_7 gets string_size_6's noteway draw layer (11 in stock), every frame while
// shown (a cheap walk of two property lists; only a difference is written).
void LayerExtra(Head& h, int e) {
    const uint8_t* from = h.altSeg[0] ? h.altSeg[0] : h.stockSeg[0];  // string_size_6
    uint16_t value = 0, was = 0;
    const int rc = rsxrasset::CopyLayer(from, h.segX[e], rsxrasset::GamePropType, &value, &was);

    if (rc == 1 && RsxrLog_Upto(&g_head.logLayer, 4)) {
        RsxrLog("assetload: guitar %p: string_size_%d draw layer %u -> %u (string_size_6's; it drew after the fret "
                "bars, over the nut - issue #23)",
                h.guitar, kSegs + 1 + e, unsigned(was), unsigned(value));
    } else if (rc < 0 && RsxrLog_Upto(&g_head.logLayer, 4)) {
        RsxrLog("assetload: guitar %p: string_size_%d draw layer NOT set (no layer property on string_size_%d)",
                h.guitar, kSegs + 1 + e,
                rsxrasset::FindProperty(from, rsxrasset::kLayerType, rsxrasset::GamePropType) ? kSegs + 1 + e : kSegs);
    }
}

// The frame's loader gave the stock-named segments their draw layer by name (0x58E090) and never saw string_size_11
// / _6 of a 5/6-string bass (it ran at the stock count): they get the E string's, every frame while shown.
void LayerBass(Head& h) {
    for (int s = 4; h.bass && s < kSegs; ++s) {
        uint16_t value = 0, was = 0;

        if (h.altSeg[s] && rsxrasset::CopyLayer(h.altSeg[0], h.altSeg[s], rsxrasset::GamePropType, &value, &was) == 1 &&
            RsxrLog_Upto(&g_head.logLayer, 4)) {
            RsxrLog("assetload: guitar %p: bass string %d segment draw layer %u -> %u (the E string's)", h.guitar, s,
                    unsigned(was), unsigned(value));
        }
    }
}
}  // namespace rsxrasset
