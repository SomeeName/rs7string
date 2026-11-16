// The fret range glow (FRG1 / FRG2 / FRG2B / FRG3): the glow that lights the fret wires at both ends of a hand
// position, and the box between them, reach the strings beyond the stock ones. Split from hooks.cpp; the bar geometry
// it reads is there.
#include "features/frets/hooks.h"
#include "features/frets/state.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <intrin.h>
#include <iterator>

using namespace rs7frets;
using namespace rs7fretglue;

namespace {
void GlowEndsRaw(uint8_t* fx, const uint8_t* player) {
    uint8_t* guitar = *reinterpret_cast<uint8_t* const*>(player + rs7::kSessionGuitar);
    const int lanes = ExtraLanes(guitar);
    if (lanes < 1) {
        return;
    }

    const float *a0, *a1, *w0, *w1;
    uint8_t* grid = UncheckedField<uint8_t*>(guitar, kGuitarGrid);
    if (!grid || !*Bar(grid, 0) || !Attach(guitar, &a0, &a1, &w0, &w1)) {
        return;
    }

    // The floor's side: the end goes exactly to the floor (the stock margin beyond string 0 plus the extra lanes),
    // whether ShiftFloor has moved this end yet or not; adding lanes to an end it had not moved left an 8-string's
    // glow two lanes short of the floor and its bar one lane short of the 8th string (#283). The other side still
    // needs its own extra lanes.
    const float* b = reinterpret_cast<const float*>(Bar(grid, 0) + kBarBLocal);
    const int stockFar = StockFar(guitar), farLanes = FarLanes(guitar);
    const bool shifted = FloorMoved(guitar) > 0;
    float unused[3];
    const bool nearFloor = FloorShift(a0, a1, b, lanes, unused);
    float f0[3], f1[3], u[3], len2 = 0.0f, pb = 0.0f;
    int moved = 0;

    for (int i = 0; i < 3; ++i) {
        f0[i] = w0[i] + float(stockFar) * (w1[i] - w0[i]);
        f1[i] = w0[i] + float(stockFar - 1) * (w1[i] - w0[i]);
        u[i] = a0[i] - a1[i];
        len2 += u[i] * u[i];
        pb += (b[i] - a0[i]) * u[i];
    }

    const float margin = len2 > 1e-6f ? pb / len2 : 0.0f;  // the stock floor end beyond string 0, in spacings

    for (const uint32_t off : {0x10u, 0x1Cu, 0x28u, 0x34u}) {  // the low and high fret's two wire ends
        float* end = reinterpret_cast<float*>(fx + off);

        if (nearFloor) {
            moved += margin > 0.0f && GlowEndAt(w0, w1, margin + float(lanes), end) ? 1 : 0;
        } else {
            moved += GlowEnd(w0, w1, lanes, end) ? 1 : 0;
        }
        if (!shifted || nearFloor) {
            moved += GlowEnd(f0, f1, farLanes, end) ? 1 : 0;
        }
    }

    // The box (+6Ch) and the four bars (+70h..+7Ch) keep their stock size, centred between the wire ends; FRG2 / FRG2B
    // (bars) and FRG3 (box) scale each to all strings once its entity exists (#283).
    float d = 0.0f;

    for (int i = 0; i < 3; ++i) {
        d += (w0[i] - w1[i]) * (w0[i] - w1[i]);
    }

    d = std::sqrt(d);
    const float k = GlowStretch(stockFar + lanes + farLanes, d, margin * d);

    if (k > 1.0f) {
        for (const uint32_t bar : {0x6Cu, 0x70u, 0x74u, 0x78u, 0x7Cu}) {
            if (const uint8_t* obj = UncheckedField<uint8_t*>(fx, bar)) {
                g_state.glowBars[g_state.glowNext] = {obj, k};
                g_state.glowNext = (g_state.glowNext + 1) % int(std::size(g_state.glowBars));
            }
        }
    }

    if (Rs7Log_Once(&g_state.logGlow)) {
        Rs7Log("frets FRG1: fret range glow %p of guitar %p: %d of 4 wire ends moved %d string spacing(s) past string "
               "0, bars x%.3f",
               fx, guitar, moved, lanes, double(k));
    }
}
}  // namespace

// FRG1 - 0x40DCCD in Rnd_FretRangeFx_Init 0x40DA40, after its four Rnd_FretGrid_GetFretWireEnd calls (EBX = the effect:
// the low fret's wire ends at +10h / +1Ch, the high fret's at +28h / +34h, world; [EBP+0Ch] = the highway, guitar at
// +37Ch). The glow that lights the fret wires at both ends of a chord's or note's hand position on the neck reads the
// stock wire ends; on an instrument with strings beyond string 0 the bars were extended past them, so the glow stopped
// short of the low B / 7th / 8th string. The thunk replays `fld [13610C8h]`; the game then derives the glow's centres
// from the moved ends.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FRG1(Rs7Mid::Context& c) {
    uint8_t* fx = reinterpret_cast<uint8_t*>(c.ebx);
    const uint8_t* player = *reinterpret_cast<const uint8_t* const*>(c.ebp + 0x0C);
    if (!g_rs7_frets_on || !fx || !player) {
        return;
    }

    __try {
        GlowEndsRaw(fx, player);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
    }
}

namespace {
void GlowBarRaw(uint8_t* obj) {
    for (GlowBar& bar : g_state.glowBars) {
        if (bar.obj != obj) {
            continue;
        }

        const float k = bar.k;

        bar = {};
        const uint32_t hash = UncheckedField<uint32_t>(obj, 0x0C);
        uint8_t* entity = UncheckedField<uint8_t*>(obj, 0x10);

        // The glow's two bar effects and its box (FRG1).
        if (entity && (hash == 0xAF25DCE0u || hash == 0x68058637u || hash == 0xFA400094u)) {
            GameSetScale(entity, k);
            if (Rs7Log_Once(&g_state.logGlowBar)) {
                Rs7Log("frets FRG2: fret range glow piece %p (effect %08X) scaled x%.3f", obj, hash, double(k));
            }
        }

        return;
    }
}
}  // namespace

// FRG2 - 0x40BAE3 in the glow effect's update 0x40B8D0, after it placed a bar being shown, and FRG2B - 0x40BC60, its
// twin after a bar was moved (the path the fret range glow takes: FRG2 alone never saw a bar). EBX = the effect object:
// +0Ch its effect hash, +10h the entity, created by now. A fret range glow bar FRG1 recorded gets the uniform scale
// that makes it span all strings (Entity_SetScale; the effect's own scale at +14h does not change its size). The thunks
// replay `mov eax, [ebx+10h]` / `push 1`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FRG2(Rs7Mid::Context& c) {
    uint8_t* obj = reinterpret_cast<uint8_t*>(c.ebx);
    if (!g_rs7_frets_on || !obj) {
        return;
    }

    __try {
        GlowBarRaw(obj);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
    }
}

// FRG3 - 0x40C5E7 in the fret range box's update 0x40C590, right after its entity was first placed (EDI = the box
// object, hash FA400094, +10h the entity). The box FRG1 recorded gets the same uniform scale as the bars: its bones sit
// on the two fret centres, its height is the stock model's (#283). The thunk replays `movzx eax, byte [edi+18h]` /
// `push eax`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_FRG3(Rs7Mid::Context& c) {
    uint8_t* obj = reinterpret_cast<uint8_t*>(c.edi);
    if (!g_rs7_frets_on || !obj) {
        return;
    }

    __try {
        GlowBarRaw(obj);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
    }
}

void rs7fretglue::ResetGlow() {
    std::fill(std::begin(g_state.glowBars), std::end(g_state.glowBars), GlowBar{});
    _InterlockedExchange(&g_state.logGlow, 0);
    _InterlockedExchange(&g_state.logGlowBar, 0);
}
