#include "features/tuner/hooks.h"
#include "features/pitch/pitch.h"
#include "features/scene/scene.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <intrin.h>

// The tuner's shared hooks (TK, TS, W, AT): the manifest key per offset array, the native 8-slot session hand-off
// (hooks_session.cpp), the wrong-instrument threshold and [debug] tuner_autotune. Game thread only. The six-slot
// B-phase tuner (low B as a phase of slot 0: T1 TA TR M1 M2 L1 L2 TLC TLI TLO1 TLO2 TF PK PE TD) was removed in 1.0.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_tuner_auto = 0;
__declspec(dllexport) volatile long g_rs7_tuner_any = 0;
__declspec(dllexport) uintptr_t g_rs7_tuner_mgr = 0;
__declspec(dllexport) uintptr_t g_rs7_lowrange = 0;
__declspec(dllexport) Rs7WrongEnt g_rs7_wrong_tab[4] = {};
__declspec(dllexport) float g_rs7_wrong_thr = 34.0f;
__declspec(dllexport) uintptr_t g_rs7_wrong_const = 0;  // X_WTHR: the address of the game's threshold (34.0)
__declspec(dllexport) uintptr_t g_rs7_fn_getdet = 0;
}

rs7tuner::State rs7tuner::g_state;

namespace {
using rs7tn::kKey;
using rs7tuner::At;
using rs7tuner::g_state;
using rs7tuner::Key;

template <class T>
T Rd(const void* base, uint32_t off) {
    T v{};

    if (base) {
        rs7s7d::SafeCopy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    }

    return v;
}

void UpdateAny() {
    long any = g_rs7_tuner_auto ? 1 : 0;

    for (Rs7WrongEnt& w : g_rs7_wrong_tab) {
        w = Rs7WrongEnt{};
    }

    if (g_rs7_native_tuner && Rs7Tuner8_WrongEntries(g_rs7_wrong_tab, 4)) {
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

// ---- the manifest key, through the game's JSON vtable (as Play_Tuning_ReadStringOffsets 0x5B84DC) ----
// tools/tests/test_manifest_readkey.py compiles this part (from the JsonFind alias to the end marker) on its own.
using JsonFind = void*(__thiscall*)(void* self, const char* name);
using JsonNum = uint8_t*(__thiscall*)(void* self);

namespace rs7tuner {
bool JsonNumber(void* obj, const char* key, double* out) {
    __try {
        if (!obj) {
            return false;
        }

        void** vt = *static_cast<void***>(obj);
        void* node = reinterpret_cast<JsonFind>(vt[0x14 / 4])(obj, key);
        if (!node) {
            return false;
        }

        void** nvt = *static_cast<void***>(node);
        uint8_t* num = reinterpret_cast<JsonNum>(nvt[0x50 / 4])(node);
        if (!num) {
            return false;
        }

        *out = *reinterpret_cast<const double*>(num + 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool JsonOffset(void* obj, const char* key, int* out, double* raw) {
    double d = 0.0;
    if (!JsonNumber(obj, key, &d) || !(d > -25.0 && d < 25.0)) {
        return false;
    }

    *out = static_cast<int>(d);  // the stock readers truncate (0xF5B0D0)
    if (raw) {
        *raw = d;
    }

    return true;
}
}  // namespace rs7tuner

namespace {
// holder = the address of the target tuning's JSON object.
bool ReadKey(void* holder, int* t6, double* raw, const char* key = kKey) {
    void* obj = nullptr;

    __try {
        obj = holder ? *static_cast<void**>(holder) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return rs7tuner::JsonOffset(obj, key, t6, raw);
}
}  // namespace

// ---- end of the manifest key reader (test_manifest_readkey.py)

namespace {
bool SessionAlive(const uint8_t* session) {
    uint8_t* mgr = g_rs7_tuner_mgr ? *reinterpret_cast<uint8_t**>(g_rs7_tuner_mgr) : nullptr;
    if (!mgr) {
        return false;
    }

    for (uint32_t i = 0; i < 4; ++i) {
        if (At<uint8_t*>(mgr, 0x10 + i * 4) == session) {
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

    for (const Key& k : g_state.keys) {
        if (k.offs == offs && k.found) {
            *t6 = k.t6;
            return true;
        }
    }

    return false;
}

void Rs7Tuner_ResetAll() {
    for (Key& k : g_state.keys) {
        k = Key{};
    }

    for (rs7tuner::Auto& a : g_state.autos) {
        a = rs7tuner::Auto{};
    }

    UpdateAny();
}

namespace {
void PublishedTarget(const uint8_t* session, int* n, int extra[2]);
}

// ---- stub entry points ----
extern "C" void __cdecl Rs7_TunerKey(int* offs, void* holder) {
    if (!offs) {
        return;
    }

    Key k{offs, false, 0, 0.0};

    k.found = ReadKey(holder, &k.t6, &k.raw);
    double raw7 = 0.0;

    if (k.found) {
        k.found7 = ReadKey(holder, &k.t7, &raw7, rs7tn::kKey8);
    }
    Key* slot = nullptr;

    for (Key& x : g_state.keys) {
        if (x.offs == offs) {
            slot = &x;
        }
    }

    if (!slot) {
        for (Key& x : g_state.keys) {
            if (!slot && !x.offs) {
                slot = &x;
            }
        }
    }

    if (!slot) {
        slot = &g_state.keys[0];
    }
    const bool changed = slot->offs != offs || slot->found != k.found || slot->t6 != k.t6 || slot->found7 != k.found7 ||
                         slot->t7 != k.t7;

    *slot = k;

    if (changed) {
        Log("tuner: target tuning read into %p: %s", offs,
            k.found ? (k.found7 ? "manifest keys Rs7String6 and Rs7String7 present (8-string arrangement)"
                                : "manifest key Rs7String6 present (7-string arrangement)")
                    : "no Rs7String6 (6-string)");
    }
}

extern "C" void __cdecl Rs7_TunerApply(uint8_t* session, const int* offs) {
    if (!g_rs7_native_tuner || !session || !offs) {
        return;  // without the native 8-slot session the stock six-string tuner runs
    }

    const Key* k = nullptr;

    for (const Key& x : g_state.keys) {
        if (x.offs == offs) {
            k = &x;
        }
    }

    int n = !k ? 6 : !k->found ? 6 : k->found7 ? 8 : 7;
    int extra[2] = {k && k->found ? k->t6 : 0, k && k->found7 ? k->t7 : 0};

    PublishedTarget(session, &n, extra);
    if (!k && n <= 6) {
        return;  // not read by TK and nothing published: keep the state
    }

    Rs7Tuner8_Apply(session, offs, n, extra);
}

// W: called inside pushad with the tuner's x87 stack live (ST0 = the mean). Integer and SSE only
// (a float assignment compiles to movss; no CRT, no logging, no exception frame).
extern "C" void __cdecl Rs7_WrongThreshold(uint8_t* session) {
    int thr = 34;

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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Lowest-open policy (detector +11F8h = string0 + 40 from SetTuning): lowered to the session's lowest
// open note, never raised. Only a guitar detector linked to its matcher (as Rs7_OnSongTuning checks).
void LowerDetectorTo(const uint8_t* session, int low) {
    uint8_t* det = static_cast<uint8_t*>(GetDetector(Rd<uint32_t>(session, 0xC)));
    if (!det) {
        return;
    }

    uint8_t* input = At<uint8_t*>(det, rs7::kDetOwner);
    uint8_t* matcher = input ? At<uint8_t*>(input, rs7::kInputMatcher) : nullptr;
    if (!matcher || At<uint8_t*>(matcher, rs7::kMatcherDetector) != det) {
        return;
    }
    if (At<int>(det, rs7::kDetInstrument) == 2) {
        return;
    }

    int& cur = At<int>(det, rs7::kDetLowestOpen);
    if (low >= cur) {
        return;
    }

    Log("tuner: low range: detector %p lowest open %d -> %d, wrong-instrument threshold %d", det, cur, low,
        rs7tuner::WrongThreshold(low));
    cur = low;
}

// [neck] native_tuner, no manifest key: a count feat/publish published on the session's guitar
// (InstrumentInfo +0Bh 7 / 8) with the 7th / 8th offsets of the detector's tuning8 tail (pitch8).
void PublishedTarget(const uint8_t* session, int* n, int extra[2]) {
    __try {
        const int published = int(Rs7Tuner8_GuitarCount(session));
        if (published < 7 || published > 8 || published <= *n) {
            return;
        }

        uint8_t* det = static_cast<uint8_t*>(GetDetector(Rd<uint32_t>(session, 0xC)));
        const bool tail = det && Rs7Pitch_Tailed(det, 1);

        for (int s = *n; s < published; ++s) {
            extra[s - 6] = tail ? Rd<int16_t>(det, rs7p::kDetTuning8 + 2 * uint32_t(s)) : 0;
        }

        Log("tuner: native tuner: no manifest key for %d strings, the guitar's published count %d (offsets %s)", *n,
            published, tail ? "from the detector's tuning8" : "0: no detector tail");
        *n = published;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

        if (GetTickCount() - x.tick < uint32_t(g_rs7_tuner_auto) * 1000u) {
            return 0;
        }

        x.tick = GetTickCount();  // once per interval
        Log("tuner: [debug] tuner_autotune: string %d reported tuned", x.cur);
        return 1;
    }

    // The session's first query (or autotune was switched on during it): its interval starts now.
    if (free && session) {
        *free = rs7tuner::Auto{session, GetTickCount(), At<int>(session, rs7tu::kCur)};
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
    Rs7Tuner_ResetAll();
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
           c.tunerNames ? ("7-string tuning names, e.g. \"" + rs7tn::KnownDisplay(b0, tn, 0, false) + "\"").c_str()
                        : "stock tuning names");
}
