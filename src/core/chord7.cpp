#include "core/chord7.h"
#include "core/chordloops.h"
#include "core/config.h"
#include "core/frets.h"
#include "core/mode.h"
#include "core/nslot.h"
#include "core/nsmatch.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace rs7chord {

bool Decode(uint8_t b, Ext* out) {
    const int fret = (b & 0x1F) - 1;
    const int finger = (b >> 5) - 1;
    if (fret < 0) {
        return false;  // 0 = no column (a finger without a fret is malformed: ignored too)
    }

    out->fret = fret;
    out->finger = finger;

    return true;
}

uint8_t Encode(int fret, int finger) {
    if (fret < 0 || fret > 30) {
        return 0;
    }

    if (finger < -1 || finger > 6) {
        finger = -1;
    }

    return static_cast<uint8_t>(((finger + 1) << 5) | (fret + 1));
}

bool FromTemplateBytes(const uint8_t* tpl, Ext* out) {
    if (!std::memchr(tpl + kTplName, 0, 31)) {
        return false;  // a 31+ character name: not ours
    }

    return Decode(tpl[kTplExt], out);
}

}  // namespace rs7chord

namespace {
volatile LONG g_active = 0;  // 7-string arrangement running (S4)
volatile LONG g_t6 = 0;

bool SafeRead(const void* p, void* out, size_t n) {
    if (!p) {
        return false;
    }

    __try {
        std::memcpy(out, p, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Logging(volatile LONG* counter, LONG max) {
    return InterlockedIncrement(counter) <= max;
}
}  // namespace

namespace {
int g_frameAxis = 1;  // [chords] frame_axis
constexpr int kMarks = 32;
constexpr float kFrameSign =
    1.0f;  // HYPOTHESIS (verify in game): model +axis points from the frame's origin edge to the lanes
const void* g_boxMarks[kMarks] = {};
const void* g_boxGuitar[kMarks] = {};  // the Rnd_Guitar the marked frame is drawn for
uint8_t g_boxTop[kMarks] = {};         // highest string the marked chord uses: 5 (lanes 0-5 only), 6 or 7
int g_boxNext = 0;
volatile LONG g_songStrings = 6;  // S4: the arrangement's string count (bass 4); read by B1 under CL*
volatile LONG g_boxLogged = 0;

bool ExtOfObject(const void* obj, uint32_t tplOffset, rs7chord::Ext* x, const void** tplOut = nullptr) {
    const void* tpl = nullptr;
    if (!SafeRead(static_cast<const uint8_t*>(obj) + tplOffset, &tpl, sizeof tpl)) {
        return false;
    }

    if (tplOut) {
        *tplOut = tpl;
    }

    return Rs7Chord_Get(tpl, x);
}
}  // namespace

void Rs7Chord_OnSong(bool active, int t6) {
    const Rs7MatchStats st = Rs7Ns_MatchStats(true);

    if (st.stock6 || st.portN) {
        Rs7Log("chords: matcher calls since the last song setup: %ld with up to six targets (stock body), %ld with "
               "seven or eight (N-target port, %ld matched)",
               st.stock6, st.portN, st.portHits);
    }

    InterlockedExchange(&g_t6, t6);
    InterlockedExchange(&g_active, active ? 1 : 0);
    InterlockedExchange(&g_boxLogged, 0);

    for (auto& m : g_boxMarks) {
        m = nullptr;
    }
}

bool Rs7Chord_Get(const void* tpl, rs7chord::Ext* out) {
    if (!g_active) {
        return false;
    }

    const uint8_t* t8 = Rs7Ns_AsT8(tpl);
    if (!t8) {
        return false;
    }

    const int8_t fret = static_cast<int8_t>(t8[rs7ns::kT8Frets + 6]);
    if (fret < 0) {
        return false;
    }

    out->fret = fret;
    out->finger = static_cast<int8_t>(t8[rs7ns::kT8Fingers + 6]);

    return true;
}

int Rs7Chord_Midi6(int fret) {
    return rs7::kLowB + static_cast<int>(g_t6) + fret;
}

// ---------------------------------------------------------------- chord frame span (game thread)

float Rs7Chord_ParseFrameScale(const wchar_t* text) {
    // Empty, non-numeric, zero, negative and out-of-range text all fall back to the default.
    if (!text) {
        return 1.134f;
    }

    wchar_t* end = nullptr;
    const double s = wcstod(text, &end);
    if (end == text || !(s >= 0.5 && s <= 3.0)) {
        return 1.134f;
    }

    return static_cast<float>(s);
}

void Rs7Chord_Configure(const wchar_t* ini) {
    wchar_t buf[64];

    GetPrivateProfileStringW(L"chords", L"frame_scale", L"", buf, 64, ini);
    const bool scaleSet = buf[0] != 0;
    const int a = Rs7Config_ReadInt(L"chords", L"frame_axis", 1, ini, 2);

    g_frameAxis = a >= 0 && a <= 2 ? a : 1;
    const int m = Rs7Config_ReadInt(L"chords", L"wide_misses", 2, ini, 2);

    g_rs7_match_misses = static_cast<uintptr_t>(m >= 0 && m <= 2 ? m : 2);
    Rs7Log("config: [chords] frame_axis=%d (chord frames follow the lane positions of the guitar; [chords] frame_scale "
           "is %s: no longer used), wide_misses=%d (seven/eight-note chords may miss up to %d notes on any string; "
           "5/6-note chords one and 2-4-note chords none, as in stock)",
           g_frameAxis, scaleSet ? "set but ignored" : "not set", int(g_rs7_match_misses), int(g_rs7_match_misses));
}

void Rs7Chord_SetSongStrings(int n) {
    InterlockedExchange(&g_songStrings, n);
}

int Rs7Chord_FrameTop(const void* tpl) {
    const LONG n = g_songStrings;
    if (!g_rs7_native_chordloops || n < 7 || n > 8) {
        return -1;
    }

    const uint8_t* t8 = Rs7Ns_AsT8(tpl);
    if (!t8) {
        return -1;
    }

    for (int s = static_cast<int>(n) - 1; s >= 6; --s) {
        if (static_cast<int8_t>(t8[rs7ns::kT8Frets + s]) >= 0) {
            return s;
        }
    }

    return -1;
}

extern "C" void __cdecl Rs7Chord_OnBoxGeom(void* geom, const void* chordObj, const void* player) {
    try {
        if (!geom || !chordObj || !player) {
            return;
        }

        // Every chord frame of a 7/8-string arrangement is marked: B2 derives its span from the lane positions, so a
        // chord with strings 0-5 only keeps the stock frame on a floor that F1 moved, a chord with string 6/7 reaches
        // it.
        const bool seven = g_active || (g_rs7_native_chordloops && g_songStrings >= 7 && g_songStrings <= 8);
        if (!seven) {
            return;
        }

        int top = -1;

        if (g_rs7_native_chordloops) {
            // [chords] native_chordloops: the highest added string (6 or 7) the chord uses in its T8 record.
            const void* tpl = nullptr;
            if (!SafeRead(static_cast<const uint8_t*>(chordObj) + 0x30, &tpl, sizeof tpl)) {
                return;
            }

            top = Rs7Chord_FrameTop(tpl);
        }

        if (top < 0) {
            rs7chord::Ext x;

            top = ExtOfObject(chordObj, 0x30, &x) ? 6 : 5;
        }

        const void* guitar = nullptr;
        if (!SafeRead(static_cast<const uint8_t*>(player) + 0x37C, &guitar, sizeof guitar) || !guitar) {
            return;
        }

        g_boxMarks[g_boxNext] = geom;
        g_boxGuitar[g_boxNext] = guitar;
        g_boxTop[g_boxNext] = static_cast<uint8_t>(top);
        g_boxNext = (g_boxNext + 1) % kMarks;
    } catch (...) {
    }
}

extern "C" int __cdecl Rs7Chord_BoxScale(const void* geom) {
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
        rs7frets::FrameFit fit;

        if (!Rs7Frets_ChordFrame(static_cast<const uint8_t*>(g_boxGuitar[i]), top, &fit)) {
            if (Logging(&g_boxLogged, 4)) {
                Rs7Log("chords B2: chord frame %p: no lane geometry for guitar %p, stock frame", geom, g_boxGuitar[i]);
            }
            return 0;
        }

        if (std::fabs(fit.scale - 1.0f) < 1e-4f && std::fabs(fit.shift) < 1e-3f) {
            return 0;  // exactly the stock frame
        }

        float v1[3], v2[3];
        const uint8_t* g = static_cast<const uint8_t*>(geom);
        if (!SafeRead(g + 0x20, v1, sizeof v1) || !SafeRead(g + 0x2C, v2, sizeof v2)) {
            return 0;
        }

        // Stock Rnd_GemGeom_ApplySpan 0x7AEE80: scale (1, 1, |-v2.y + v1.y|), translate (0, 0, (-v1.y - v2.y) / 2).
        g_rs7_box_scale[0] = 1.0f;
        g_rs7_box_scale[1] = 1.0f;
        g_rs7_box_scale[2] = std::fabs(-v2[1] - -v1[1]);
        g_rs7_box_trans[0] = 0.0f;
        g_rs7_box_trans[1] = 0.0f;
        g_rs7_box_trans[2] = (-v1[1] + -v2[1]) * 0.5f;
        // The frame's extent (origin edge at the model's 0, growing towards the lanes) in model units: scale about
        // the origin edge, then translate (HYPOTHESIS: model +axis = the direction the frame extends).
        g_rs7_box_scale[g_frameAxis] *= fit.scale;
        g_rs7_box_trans[g_frameAxis] += fit.shift * kFrameSign;

        if (Logging(&g_boxLogged, 12)) {
            Rs7Log(
                "chords B2: chord frame %p up to string %d: span scale (%.3f %.3f %.3f) translate (%.3f %.3f %.3f) - "
                "axis %d: x %.4f, origin edge moved %.4f (guitar %p: d %.4f m %.4f origin p %.4f; floor locator B "
                "local "
                "(%.3f %.3f %.3f) world now (%.3f %.3f %.3f), string 0 attach z %.3f)",
                geom, top, double(g_rs7_box_scale[0]), double(g_rs7_box_scale[1]), double(g_rs7_box_scale[2]),
                double(g_rs7_box_trans[0]), double(g_rs7_box_trans[1]), double(g_rs7_box_trans[2]), g_frameAxis,
                double(fit.scale), double(fit.shift * kFrameSign), g_boxGuitar[i], double(fit.spacing),
                double(fit.margin), double(fit.origin), double(fit.bLocal[0]), double(fit.bLocal[1]),
                double(fit.bLocal[2]), double(fit.bWorld[0]), double(fit.bWorld[1]), double(fit.bWorld[2]),
                double(fit.a0[2]));
        }

        return 1;
    } catch (...) {
        return 0;
    }
}
