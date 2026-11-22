// Chord glue: the 7-string arrangement state (S4) and the chord frame span (B1/B2 callbacks, the B2 stub is in
// hooks_stubs.cpp); reads [chords] wide_misses.
#include "features/chords/chords.h"
#include "features/frets/frets.h"
#include "features/nslot/nslot.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/settings.h"
#include <windows.h>
#include <cmath>

namespace {
constexpr int kFrameAxis = 1;  // the frame model's string axis (y; the former [chords] frame_axis default)
constexpr int kMarks = 32;
const void* g_boxMarks[kMarks] = {};
const void* g_boxGuitar[kMarks] = {};  // the Rnd_Guitar the marked frame is drawn for
uint8_t g_boxTop[kMarks] = {};         // the string the marked frame reaches: the instrument's last (bass: 6)
int g_boxNext = 0;
volatile LONG g_boxLogged = 0;
}  // namespace

void RsxrChord_OnSong() {
    const RsxrMatchStats st = RsxrNs_MatchStats(true);

    if (st.stock6 || st.portN) {
        RsxrLog("chords: matcher calls since the last song setup: %ld with up to six targets (stock body), %ld with "
                "seven or eight (N-target port, %ld matched)",
                st.stock6, st.portN, st.portHits);
    }

    InterlockedExchange(&g_boxLogged, 0);

    for (auto& m : g_boxMarks) {
        m = nullptr;
    }
}

// The startup value; settings.cpp's ApplyMisses only runs on later live changes (RsxrSet_Commit).
void RsxrChord_Configure() {
    g_rsxr_match_misses = static_cast<uintptr_t>(RsxrSet_Int("chords.wide_misses"));  // the table's range: 0-2
    RsxrLog("config: [chords] wide_misses=%d (seven/eight-note chords may miss up to %d notes on any string; 5/6-note "
            "chords one and 2-4-note chords none, as in stock)",
            int(g_rsxr_match_misses), int(g_rsxr_match_misses));
}

void RsxrChord_StartLoops(bool nativeStrings) {
    g_rsxr_native_chordloops = nativeStrings ? 1 : 0;
    RsxrLog("config: chord loops -> %s", g_rsxr_native_chordloops
                                             ? "on: native 8-slot chord bends / sustain techniques (CL* sites), "
                                               "7/8-string chord frames (B1/B2)"
                                             : "off (native_strings unavailable; CL* sites not written)");
}

// ---------------------------------------------------------------- chord frame span (game thread)

extern "C" void __cdecl RsxrChord_OnBoxGeom(void* geom, const void* chordObj, const void* player) {
    try {
        if (!geom || !chordObj || !player) {
            return;
        }

        const void* guitar = nullptr;
        const uint8_t* info = nullptr;
        uint8_t kind = 0, count = 0;

        if (!Rsxr_SafeCopy(&guitar, static_cast<const uint8_t*>(player) + rsxr::kSessionGuitar, sizeof guitar) ||
            !guitar || !Rsxr_SafeCopy(&info, static_cast<const uint8_t*>(guitar) + rsxr::kGuitarInfo, sizeof info) ||
            !info || !Rsxr_SafeCopy(&kind, info + rsxr::kInfoKind, 1) ||
            !Rsxr_SafeCopy(&count, info + rsxr::kInfoStrings, 1)) {
            return;
        }

        // Every chord frame of an extended instrument covers the whole instrument, like the stock frame covers the
        // stock one: B2 derives its span from the lane positions. A 7/8-string guitar: up to its last string (6 / 7),
        // whatever strings the chord uses. A 5/6-string bass: low B .. G / high C (B2 with top 6: the low B one lane
        // beyond string 0; frets: the high C beyond the G).
        const bool bass = rsxr::IsBassKind(kind) && rsxr::IsExtendedBass(count);
        const bool seven = !rsxr::IsBassKind(kind) && (count == 7 || (count == 8 && g_rsxr_native_chordloops));
        if (!seven && !bass) {
            return;
        }

        const int top = bass ? 6 : count - 1;

        g_boxMarks[g_boxNext] = geom;
        g_boxGuitar[g_boxNext] = guitar;
        g_boxTop[g_boxNext] = static_cast<uint8_t>(top);
        g_boxNext = (g_boxNext + 1) % kMarks;
    } catch (...) {
    }
}

extern "C" int __cdecl RsxrChord_BoxScale(const void* geom) {
    try {
        if (!geom) {
            return 0;
        }

        int i = 0;

        while (i < kMarks && g_boxMarks[i] != geom) {
            ++i;
        }

        if (i == kMarks) {
            return 0;
        }

        g_boxMarks[i] = nullptr;  // one-shot: Rnd_GemGeom_Build runs once per component
        const int top = g_boxTop[i];
        rsxrfrets::FrameFit fit;

        if (!RsxrFrets_ChordFrame(static_cast<const uint8_t*>(g_boxGuitar[i]), top, &fit)) {
            if (RsxrLog_Upto(&g_boxLogged, 4)) {
                RsxrLog("chords B2: chord frame %p: no lane geometry for guitar %p, stock frame", geom, g_boxGuitar[i]);
            }
            return 0;
        }

        if (std::fabs(fit.scale - 1.0f) < 1e-4f && std::fabs(fit.shift) < 1e-3f) {
            return 0;  // exactly the stock frame
        }

        float v1[3], v2[3];
        const uint8_t* g = static_cast<const uint8_t*>(geom);
        if (!Rsxr_SafeCopy(v1, g + 0x20, sizeof v1) || !Rsxr_SafeCopy(v2, g + 0x2C, sizeof v2)) {
            return 0;
        }

        // Stock Rnd_GemGeom_ApplySpan 0x7AEE80: scale (1, 1, |-v2.y + v1.y|), translate (0, 0, (-v1.y - v2.y) / 2).
        g_rsxr_box_scale[0] = 1.0f;
        g_rsxr_box_scale[1] = 1.0f;
        g_rsxr_box_scale[2] = std::fabs(v1[1] - v2[1]);
        g_rsxr_box_trans[0] = 0.0f;
        g_rsxr_box_trans[1] = 0.0f;
        g_rsxr_box_trans[2] = (-v1[1] + -v2[1]) * 0.5f;
        // The frame's extent (origin edge at the model's 0, growing towards the lanes) in model units: scale about
        // the origin edge, then translate (HYPOTHESIS: model +axis = the direction the frame extends).
        g_rsxr_box_scale[kFrameAxis] *= fit.scale;
        g_rsxr_box_trans[kFrameAxis] += fit.shift;

        if (RsxrLog_Upto(&g_boxLogged, 12)) {
            RsxrLog(
                "chords B2: chord frame %p up to string %d: span scale (%.3f %.3f %.3f) translate (%.3f %.3f %.3f) - "
                "axis %d: x %.4f, origin edge moved %.4f (guitar %p: d %.4f m %.4f origin p %.4f; floor locator B "
                "local (%.3f %.3f %.3f) world now (%.3f %.3f %.3f), string 0 attach z %.3f)",
                geom, top, double(g_rsxr_box_scale[0]), double(g_rsxr_box_scale[1]), double(g_rsxr_box_scale[2]),
                double(g_rsxr_box_trans[0]), double(g_rsxr_box_trans[1]), double(g_rsxr_box_trans[2]), kFrameAxis,
                double(fit.scale), double(fit.shift), g_boxGuitar[i], double(fit.spacing), double(fit.margin),
                double(fit.origin), double(fit.bLocal[0]), double(fit.bLocal[1]), double(fit.bLocal[2]),
                double(fit.bWorld[0]), double(fit.bWorld[1]), double(fit.bWorld[2]), double(fit.a0[2]));
        }

        return 1;
    } catch (...) {
        return 0;
    }
}
