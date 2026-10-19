#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/headstock/assetload_head.h"
#include "features/headstock/assetload_scene.h"
#include "features/string7/string7.h"
#include "model/groups.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// The six headstock string segments of the edited model and the 7th: shader params, colour, draw layer.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_strparams = 0;
}

using namespace rs7asset;

namespace {
const int kInfoGuitar[4] = {1, 0, 0, 0};  // Rnd_SetStringShaderParams only reads info+0 (2 = bass)
volatile long g_logBind = 0, g_logSeg7 = 0, g_logLayer = 0;
constexpr uint32_t kStrShapeFound = 0x50;
constexpr uint32_t kStrColour = 0x38;

// Rnd_SetStringShaderParams 0x5998F0 (EAX = param object, cdecl (info, slot, mesh); it releases the
// mesh once, so it is AddRef'd first, as 0x58E2BB..0x58E2C1 does): sets the segment's shader extra
// data and points the param object at it, so the game's per-frame segment colour reaches it.
bool BindSegment(void* params, uint8_t* mesh, uint32_t slot) {
    if (!g_rs7_fn_strparams || !mesh || !rs7s7d::IsKindOf(mesh, "NiMesh")) {
        return false;
    }

    __try {
        AddRef(mesh);
        const int* info = kInfoGuitar;

        __asm {
            push mesh
            push slot
            push info
            mov eax, params
            call dword ptr [g_rs7_fn_strparams]
            add esp, 0Ch
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    return Rd<uintptr_t*>(head, kHeadParams + 4u * uint32_t(s));
}
}  // namespace

namespace rs7asset {
// Once per frame instance: our own param holders for the alt's segments (the stock holders are not
// touched yet) and a referenced copy of the stock holders' pointers.
void BindAlt(Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, kGuitarHead);
    int n = 0;

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = StockParams(head, s);

        h.segOk[s] = false;
        if (!params || !h.altSeg[s]) {
            continue;
        }

        std::memset(h.altHold[s], 0, sizeof h.altHold[s]);
        if (!BindSegment(h.altHold[s], h.altSeg[s], uint32_t(s))) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            h.stockHold[s][i] = 0;
            uintptr_t p = 0;
            if (!rs7s7d::SafeCopy(&p, params + i, sizeof p) || !p || !h.altHold[s][i]) {
                continue;
            }

            AddRef(reinterpret_cast<void*>(p));
            h.stockHold[s][i] = p;
        }

        h.segOk[s] = true;
        ++n;
    }

    if (h.seg7) {
        BindSegment(h.params7, h.seg7, 0);  // string 0's V row, like the 7th string line
    }
    h.bound = true;
    Rs7Alloc_Inc(kAkAssetBinds);
    if (Upto(&g_logBind, 8)) {
        Rs7Log("assetload: guitar %p: %d of %d headstock string segment(s) of the edited model bound%s", h.guitar, n,
               kSegs, h.seg7 ? "; string_size_7 bound to its own params" : "; no string_size_7");
    }
}

// The game's per-frame segment state goes through the six stock param objects: point them at the
// alt's extra data (toAlt) or back, carrying the current values over, so nothing is reset.
void PointParams(Head& h, bool toAlt) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, kGuitarHead);

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = StockParams(head, s);
        if (!params || !h.segOk[s]) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            const uintptr_t from = toAlt ? h.stockHold[s][i] : h.altHold[s][i];
            const uintptr_t to = toAlt ? h.altHold[s][i] : h.stockHold[s][i];
            if (!from || !to) {
                continue;
            }

            uint8_t v[16];

            if (rs7s7d::SafeCopy(v, reinterpret_cast<uint8_t*>(from) + kColourRgb, ValueSize(i))) {
                rs7s7d::SafeStore(reinterpret_cast<uint8_t*>(to) + kColourRgb, v, ValueSize(i));
            }
            SwapRef(params + i, to);
        }
    }

    h.pointed = toAlt;
}

void FindSegments(Head& h) {
    // The stock model: the first NiNode sibling of rs7_alt (culled or not, so the restore always finds it).
    uint8_t* stock = nullptr;

    EachChild(Rd<uint8_t*>(h.alt, kParent), [&](uint8_t* c) {
        if (c != h.alt && rs7s7d::IsKindOf(c, "NiNode")) {
            stock = c;
        }

        return !stock;
    });

    for (int s = 0; s < kSegs; ++s) {
        char name[24];

        std::snprintf(name, sizeof name, "string_size_%d", kSegs - s);
        h.altSeg[s] = FindByName(h.alt, name);
        h.stockSeg[s] = stock ? FindByName(stock, name) : nullptr;
    }

    h.seg7 = FindByName(h.alt, "string_size_7");
}

// The 7th segment's colour, every shown frame: what Rnd_Headstock_UpdateSegmentColours 0x58E4F0
// does for segment s < stringCount (string object +50h base shape found: copy the colour handle
// +38h's RGBA (+14h..+23h) onto the segment's g_diffuseColor), for the 7th string's object S7. So it
// is lit / dimmed exactly like the 7th string line (N11). Without S7 ([neck] string7_line=0) the
// static [color] string6 colour.

void Colour7(Head& h) {
    uint8_t* cd = reinterpret_cast<uint8_t*>(h.params7[3]);
    if (!cd) {
        return;
    }

    uint8_t* s7 = Rs7S7_ObjectOf(h.guitar);
    uint8_t* src = (s7 && Rd<uint8_t>(s7, kStrShapeFound)) ? Rd<uint8_t*>(s7, kStrColour) : nullptr;
    uint8_t rgba[16];

    if (src && rs7s7d::SafeCopy(rgba, src + kColourRgb, sizeof rgba)) {
        rs7s7d::SafeStore(cd + kColourRgb, rgba, sizeof rgba);
        if (Upto(&g_logSeg7, 2)) {
            Rs7Log("assetload: guitar %p: string_size_7 follows the 7th string's colour (S7 %p colour %p)", h.guitar,
                   s7, src);
        }
    } else {
        rs7s7d::SafeStore(cd + kColourRgb, g_rs7_fx_rgb, 3 * sizeof(float));
    }
}

// The 7th segment at the nut (issue #17 / #23 / nut gap, 2026-10-15). string_size_7 is left exactly as
// the model has it: its nut end equals string_size_1..6's (model y 15.21, same height and radius,
// pitch 1.608 in z) and lands on the 7th neck string's start (Rs7Gs_AttachExt, features/strings/strings.h
// ExtrapolateAttach: z to 0.001, y to 0.011), like strings 0-5 meet theirs. An older workaround
// sheared the segment's nut end 0.30 deeper (x) on the idea that the 7th nut slot is lower; the real
// cause of "the segment draws in front of the nut" was the draw layer (Layer7 below, CONFIRMED in game
// by issue #23), so the shear only left the thick segment end 0.30 off the neck string's axis, more
// than the end radius (0.20): the visible gap at the nut. It is gone.

// Issue #23: string_size_7 gets string_size_6's noteway draw layer (11 in stock), every frame while
// shown (a cheap walk of two property lists; only a difference is written).
void Layer7(Head& h) {
    const uint8_t* from = h.altSeg[0] ? h.altSeg[0] : h.stockSeg[0];  // string_size_6
    uint16_t value = 0, was = 0;
    const int rc = rs7asset::CopyLayer(from, h.seg7, rs7asset::GamePropType, &value, &was);

    if (rc == 1 && Upto(&g_logLayer, 2)) {
        Rs7Log("assetload: guitar %p: string_size_7 draw layer %u -> %u (string_size_6's; it drew after the fret "
               "bars, over the nut - issue #23)",
               h.guitar, unsigned(was), unsigned(value));
    } else if (rc < 0 && Upto(&g_logLayer, 2)) {
        Rs7Log("assetload: guitar %p: string_size_7 draw layer NOT set (no layer property on string_size_%s)", h.guitar,
               rs7asset::FindProperty(from, rs7asset::kLayerType, rs7asset::GamePropType) ? "7" : "6");
    }
}
}  // namespace rs7asset
