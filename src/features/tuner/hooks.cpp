#include "features/tuner/hooks.h"
#include "features/colours/colours.h"
#include "features/pitch/pitch.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "model/instrument.h"
#include "model/manifest.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <intrin.h>
#include <iterator>

// The tuner's shared hooks (TK, TS, W, AT): the manifest key per offset array, the native 8-slot session hand-off
// (hooks_session.cpp), the wrong-instrument threshold and [debug] tuner_autotune. Game thread only.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_tuner_auto = 0;
__declspec(dllexport) volatile long g_rs7_tuner_any = 0;
__declspec(dllexport) uintptr_t g_rs7_tuner_mgr = 0;
__declspec(dllexport) uintptr_t g_rs7_lowrange = 0;
__declspec(dllexport) Rs7WrongEnt g_rs7_wrong_tab[4] = {};
__declspec(dllexport) float g_rs7_wrong_thr = float(rs7tuner::kStockWrongThreshold);
__declspec(dllexport) uintptr_t g_rs7_wrong_const = 0;  // X_WTHR: the address of the game's threshold (34.0)
__declspec(dllexport) uintptr_t g_rs7_fn_getdet = 0;
}

rs7tuner::State rs7tuner::g_state;

namespace {
using rs7tn::kKey;
using rs7tuner::g_state;
using rs7tuner::Key;
using rs7tuner::UncheckedField;

void UpdateAny() {
    long any = g_rs7_tuner_auto ? 1 : 0;

    for (Rs7WrongEnt& w : g_rs7_wrong_tab) {
        w = Rs7WrongEnt{};
    }

    if (Rs7Tuner8_WrongEntries(g_rs7_wrong_tab, int(std::size(g_rs7_wrong_tab)))) {
        any = 1;
    }
    _InterlockedExchange(&g_rs7_tuner_any, any);
}

__declspec(noinline) void Log(const char* fmt, ...) {
    try {
        char buf[512];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        Rs7Log("%s", buf);
    } catch (...) {
    }
}
}  // namespace

// ---- the manifest key reader: ReadKey over model/manifest.h. tools/tests/test_manifest_readkey.py compiles
// model/manifest.h and this part (from here to the end marker) on its own.
namespace {
// holder = the address of the target tuning's JSON object.
bool ReadKey(void* holder, int* t6, double* raw, const char* key = kKey) {
    void* obj = nullptr;

    __try {
        obj = holder ? *static_cast<void**>(holder) : nullptr;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return false;
    }

    return rs7::JsonOffset(obj, key, t6, raw);
}
}  // namespace

// ---- end of the manifest key reader (test_manifest_readkey.py)

namespace {
bool SessionAlive(const uint8_t* session) {
    uint8_t* mgr = g_rs7_tuner_mgr ? *reinterpret_cast<uint8_t**>(g_rs7_tuner_mgr) : nullptr;
    if (!mgr) {
        return false;
    }

    constexpr uint32_t kMgrSessions = 0x10, kMgrSlots = 4;  // the tuner manager's session per player slot

    for (uint32_t i = 0; i < kMgrSlots; ++i) {
        if (UncheckedField<uint8_t*>(mgr, kMgrSessions + i * 4) == session) {
            return true;
        }
    }

    return false;
}
}  // namespace

bool Rs7Tuner_SevenFor(const int* offs, int* t6) {
    if (!offs) {
        return false;
    }

    const Key* k = g_state.keys.Find(offs);

    if (k && k->found) {
        *t6 = k->t6;
        return true;
    }

    return false;
}

int Rs7Tuner_BassFor(const int* offs) {
    if (!offs) {
        return 0;
    }

    const Key* k = g_state.keys.Find(offs);

    return k && !k->found ? k->bass : 0;
}

bool Rs7Tuner_EighthFor(const int* offs, int* t7) {
    if (!offs) {
        return false;
    }

    const Key* k = g_state.keys.Find(offs);

    if (k && k->found && k->found7) {
        *t7 = k->t7;
        return true;
    }

    return false;
}

namespace {
void ResetAll() {
    g_state.keys.Clear();

    for (rs7tuner::Auto& a : g_state.autos) {
        a = rs7tuner::Auto{};
    }

    UpdateAny();
}

void PublishedTarget(const uint8_t* session, int* n, int extra[2]);
}  // namespace

// ---- stub entry points ----
extern "C" void __cdecl Rs7_TunerKey(int* offs, void* holder) {
    if (!offs) {
        return;
    }

    Key k{false, 0, 0.0};

    k.found = ReadKey(holder, &k.t6, &k.raw);
    double raw7 = 0.0;

    if (k.found) {
        k.found7 = ReadKey(holder, &k.t7, &raw7, rs7tn::kKey8);
    } else if (int b = 0; ReadKey(holder, &b, &raw7, rs7tn::kKeyBass4)) {
        k.bass = ReadKey(holder, &b, &raw7, rs7tn::kKeyBass5) ? 6 : 5;
    }
    // The tuner shows the low B before the song setup decides it (bass hooks): without this the first extended-bass
    // tuner kept slot 4 green and the next guitar's tuner kept the last bass song's grey B string.
    const uint32_t now = GetTickCount();

    rs7colour::SetBassLowB(rs7tuner::BassLowB(k.bass != 0, g_state.guitarKeySeen, now - g_state.guitarKeyTick));

    if (!k.bass) {
        g_state.guitarKeySeen = true;
        g_state.guitarKeyTick = now;
    }

    const Key* old = g_state.keys.Find(offs);
    const bool changed = !old || old->found != k.found || old->t6 != k.t6 || old->found7 != k.found7 ||
                         old->t7 != k.t7 || old->bass != k.bass;

    g_state.keys.Put(offs, k);

    if (changed) {
        Log("tuner: target tuning read into %p: %s", offs,
            k.found  ? (k.found7 ? "manifest keys Rs7String6 and Rs7String7 present (8-string arrangement)"
                                 : "manifest key Rs7String6 present (7-string arrangement)")
            : k.bass ? "extended bass (Rs7String4)"
                     : "no Rs7String6 (6-string)");
    }
}

extern "C" void __cdecl Rs7_TunerApply(uint8_t* session, const int* offs) {
    if (!session || !offs || (!g_rs7_native_tuner && !Rs7Tuner8_IsNative(session))) {
        return;  // isolation stops new migrations; existing sessions retain their tail ownership
    }

    const Key* k = g_state.keys.Find(offs);

    int n = !k || !k->found ? 6 : k->found7 ? 8 : 7;
    int extra[2] = {k && k->found ? k->t6 : 0, k && k->found7 ? k->t7 : 0};

    if (!k) {
        PublishedTarget(session, &n, extra);
    }
    if (!k && n <= 6) {
        return;  // not read by TK and nothing published: keep the state
    }

    Rs7Tuner8_Apply(session, offs, n, extra, k ? k->bass : 0);
}

// W: called inside pushad with the tuner's x87 stack live (ST0 = the mean). Integer and SSE only
// (a float assignment compiles to movss; no CRT, no logging, no exception frame).
extern "C" void __cdecl Rs7_WrongThreshold(uint8_t* session) {
    int thr = rs7tuner::kStockWrongThreshold;

    for (const Rs7WrongEnt& w : g_rs7_wrong_tab) {
        if (w.session && w.session == session) {
            thr = w.threshold;
        }
    }

    g_rs7_wrong_thr = static_cast<float>(thr);
}

// W - `fcomp dword [1224C6Ch]` (34.0, its address from ref site X_WTHR) of UI_PlayerTuner_CheckWrongInstrument
// 0x5B2DDE, ST0 = the mean of ten detected notes, ESI = session. Full mode: it reads and edits the x87 stack. With
// [tuner] lowrange=1 and a tuner state the session's threshold decides: the replayed fcomp only yields the order of ST0
// against that constant k and pops it, so ST0 becomes k - 1, k or k + 1 by the exact order of the mean against the
// session's threshold (CompareSt0; exact for any |k| below 2^24). A NaN stays and compares unordered, as with any
// threshold. Otherwise the stock replay.
extern "C" void __cdecl Rs7_Mid_W(Rs7Mid::Context& c) {
    if (!g_rs7_lowrange || !g_rs7_tuner_any || !g_rs7_wrong_const) {
        return;
    }

    Rs7_WrongThreshold(Rs7Mid::Ptr(c.esi));
    const int order = c.CompareSt0(g_rs7_wrong_thr);

    if (order != 2) {
        const double k = *Rs7Mid::Ptr<const float>(uint32_t(g_rs7_wrong_const));

        static_cast<void>(c.SetSt0(k + order));  // cannot fail: CompareSt0 found ST0
    }
}

namespace {
using GetDetectorFn = void*(__fastcall*)(uint32_t id, void* edx);

void* GetDetector(uint32_t id) {
    __try {
        return g_rs7_fn_getdet ? reinterpret_cast<GetDetectorFn>(g_rs7_fn_getdet)(id, nullptr) : nullptr;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// Lowest-open policy (detector +11F8h = string0 + 40 from SetTuning): lowered to the session's lowest
// open note, never raised. Only a guitar detector linked to its matcher (as Rs7_OnSongTuning checks).
void LowerDetectorTo(const uint8_t* session, int low) {
    uint8_t* det = static_cast<uint8_t*>(GetDetector(Rs7_SafeRead<uint32_t>(session, rs7tu::kDetectorId)));
    if (!det) {
        return;
    }

    uint8_t* input = UncheckedField<uint8_t*>(det, rs7::kDetOwner);
    uint8_t* matcher = input ? UncheckedField<uint8_t*>(input, rs7::kInputMatcher) : nullptr;
    if (!matcher || UncheckedField<uint8_t*>(matcher, rs7::kMatcherDetector) != det) {
        return;
    }
    if (rs7::IsBassKind(UncheckedField<int>(det, rs7::kDetInstrument))) {
        return;
    }

    int& cur = UncheckedField<int>(det, rs7::kDetLowestOpen);
    if (low >= cur) {
        return;
    }

    Log("tuner: low range: detector %p lowest open %d -> %d, wrong-instrument threshold %d", det, cur, low,
        rs7tuner::WrongThreshold(low));
    cur = low;
    Rs7Pitch_DetFloor(det, 0);
}

// [neck] native_tuner, no manifest key: a count feat/publish published on the session's guitar
// (InstrumentInfo +0Bh 7 / 8) with the 7th / 8th offsets of the detector's tuning8 tail (pitch8).
void PublishedTarget(const uint8_t* session, int* n, int extra[2]) {
    __try {
        const int published = int(Rs7Tuner8_GuitarCount(session));
        if (published < 7 || published > 8 || published <= *n) {
            return;
        }

        uint8_t* det = static_cast<uint8_t*>(GetDetector(Rs7_SafeRead<uint32_t>(session, rs7tu::kDetectorId)));
        const bool tail = det && Rs7Pitch_Tailed(det, 1);

        for (int s = *n; s < published; ++s) {
            extra[s - 6] = tail ? Rs7_SafeRead<int16_t>(det, rs7p::kDetTuning8 + 2 * uint32_t(s)) : 0;
        }

        Log("tuner: native tuner: no manifest key for %d strings, the guitar's published count %d (offsets %s)", *n,
            published, tail ? "from the detector's tuning8" : "0: no detector tail");
        *n = published;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
    }
}
}  // namespace

extern "C" void Rs7Tuner_LowerDetectorTo(const uint8_t* session, int lowestOpen) {
    LowerDetectorTo(session, lowestOpen);
}

void Rs7Tuner_Refresh() {
    UpdateAny();
}

extern "C" uint32_t __cdecl Rs7_TunerAutotune(uint8_t* session) {
    if (!g_rs7_tuner_auto) {
        return 0;
    }

    rs7tuner::Auto* free = nullptr;

    for (rs7tuner::Auto& x : g_state.autos) {
        if (x.session != session) {
            if (!free && (!x.session || !SessionAlive(x.session))) {
                free = &x;
            }
            continue;
        }

        const uint32_t now = GetTickCount();
        const int cur = Rs7_SafeRead<int>(session, rs7tu::kCur);

        if (cur != x.cur) {
            x.cur = cur;
            x.tick = now;
            return 0;
        }

        if (now - x.tick < uint32_t(g_rs7_tuner_auto) * 1000u) {
            return 0;
        }

        x.tick = now;  // once per interval
        Log("tuner: [debug] tuner_autotune: string %d reported tuned", x.cur);
        return 1;
    }

    // The session's first query (or autotune was switched on during it): its interval starts now.
    if (free && session) {
        *free = rs7tuner::Auto{session, GetTickCount(), UncheckedField<int>(session, rs7tu::kCur)};
        Log("tuner: [debug] tuner_autotune=%u: session %p string %d reports tuned in %u s", unsigned(g_rs7_tuner_auto),
            session, free->cur, unsigned(g_rs7_tuner_auto));
    }

    return 0;
}

void Rs7Tuner_Stats(Rs7TunerStats* out) {
    Rs7TunerStats st{};

    for (const Rs7WrongEnt& w : g_rs7_wrong_tab) {  // the live native sessions (UpdateAny), a snapshot for display
        if (w.session) {
            ++st.sessions;
        }
    }

    st.any = g_rs7_tuner_any != 0;
    *out = st;
}

// ---- mid-hooks ----
using rs7tuner::Dword;

// TK - entry of Play_Tuning_ReadStringOffsets 0x5B8480 (ECX = int[6] out, [esp+4] = the holder of the
// target tuning JSON). The native tuner session and the tuning names (TN) need the key per array.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TK(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7_TunerKey(reinterpret_cast<int*>(c.ecx), reinterpret_cast<void*>(Dword(c.esp + 4)));
}

// TS - entry of Play_TunerSession_ApplyTuning 0x5AF200 ([esp+4] = session, EDI = the offsets).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TS(Rs7Mid::Context& c) {
    Rs7_TunerApply(reinterpret_cast<uint8_t*>(Dword(c.esp + 4)), reinterpret_cast<const int*>(c.edi));
}

// ---- startup ----
void Rs7Tuner_Configure() {
    const Rs7TunerConfig& c = Rs7Tuner_Config();

    g_rs7_lowrange = c.tunerLowRange ? 1 : 0;
    Rs7Log("config: [tuner] lowrange=%d -> %s", c.tunerLowRangeRaw,
           c.tunerLowRange
               ? "7-string tuner: wrong-instrument threshold and detector lowest open follow the lowest string"
               : "stock (wrong-instrument threshold 34)");
    g_rs7_tuner_auto = static_cast<uintptr_t>(c.tunerAutotune);
    ResetAll();
    Rs7Log("config: [debug] tuner_autotune=%d%s", c.tunerAutotune,
           c.tunerAutotune ? " s (TEST DRIVING: strings report tuned by themselves)" : " (off)");
    rs7tn::Config tn;

    tn.on = c.tunerNames;
    std::snprintf(tn.prefix, sizeof tn.prefix, "%s", c.namePrefix.c_str());
    std::snprintf(tn.suffix, sizeof tn.suffix, "%s", c.nameSuffix.c_str());
    Rs7Tn_Configure(tn);
    g_rs7_tn_on = c.tunerNames ? 1 : 0;
    const int b0[7] = {0, 0, 0, 0, 0, 0, 0};

    Rs7Log("config: [tuner] names=%d name_prefix=\"%s\" name_suffix=\"%s\"%s -> %s", c.tunerNamesRaw, tn.prefix,
           tn.suffix, c.nameAffixValid ? "" : " (INVALID text replaced by the default)",
           c.tunerNames ? ("extended tuning names, e.g. \"" + rs7tn::KnownDisplay(b0, tn, 0, false) + "\"").c_str()
                        : "stock tuning names");
}
