#include "core/tuner.h"
#include "core/mode.h"
#include "core/pitch8.h"
#include "core/string7diag.h"
#include "core/tuner8.h"
#include "core/tuningname.h"
#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <intrin.h>

// The tuner's shared hooks (TK, TS, W, AT): the manifest key per offset array, the native 8-slot session hand-off
// (tuner8.h), the wrong-instrument threshold and [debug] tuner_autotune. Game thread only. The six-slot B-phase tuner
// (low B as a phase of slot 0: T1 TA TR M1 M2 L1 L2 TLC TLI TLO1 TLO2 TF PK PE TD) was removed in 1.0.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_tuner_auto = 0;
__declspec(dllexport) volatile long g_rs7_tuner_any = 0;
__declspec(dllexport) uintptr_t g_rs7_tuner_mgr = 0;
__declspec(dllexport) uintptr_t g_rs7_fade_mgr = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_fadein = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_fadeout = 0;
__declspec(dllexport) uintptr_t g_rs7_lowrange = 0;
__declspec(dllexport) Rs7WrongEnt g_rs7_wrong_tab[4] = {};
__declspec(dllexport) float g_rs7_wrong_thr = 34.0f;
__declspec(dllexport) float g_rs7_wrong_stock = 34.0f;
__declspec(dllexport) uintptr_t g_rs7_fn_getdet = 0;
}

namespace {
using namespace rs7tuner;

// UI_PlayerTuner (0x278 bytes, docs/ARCHITECTURE.md "Session state machine").
constexpr uint32_t kGuitar = 0x2C;
constexpr uint32_t kCur = 0x74;
// Rnd_Guitar.
constexpr uint32_t kGInfo = 0x24;  // Rnd_InstrumentInfo: +0Bh stringCount
constexpr int kMax = 4;

template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

template <class T>
T Rd(const void* base, uint32_t off) {
    T v{};

    if (base) {
        rs7s7d::SafeCopy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    }

    return v;
}

struct Auto {  // [debug] tuner_autotune per session
    uint8_t* session;
    uint32_t tick;
    int cur;
};

Auto g_auto[kMax];

// TK results per offset array (menu+1B8h / +1D0h), consumed by TS.
struct Key {
    const int* offs;
    bool found;
    int t6;
    double raw;
    bool found7 = false;  // Rs7String7 (the 8th string) next to Rs7String6
    int t7 = 0;
};

Key g_keys[kMax];

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

// ---- the manifest key, through the game's JSON vtable (as Play_Tuning_ReadStringOffsets 0x5B84DC) ----
using JsonFind = void*(__thiscall*)(void* self, const char* name);
using JsonNum = uint8_t*(__thiscall*)(void* self);

bool ReadKey(void* holder, int* t6, double* raw, const char* key = kKey) {
    __try {
        void* obj = holder ? *static_cast<void**>(holder) : nullptr;
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

        const double d = *reinterpret_cast<const double*>(num + 8);
        if (!(d > -25.0 && d < 25.0)) {
            return false;
        }

        *raw = d;
        *t6 = int(d);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

// ---- pure rules ----
int rs7tuner::WrongThreshold(int lowestOpen) {
    return lowestOpen - 1 < 34 ? lowestOpen - 1 : 34;
}

bool Rs7Tuner_SevenFor(const int* offs, int* t6) {
    if (!offs) {
        return false;
    }

    for (const Key& k : g_keys) {
        if (k.offs == offs && k.found) {
            *t6 = k.t6;
            return true;
        }
    }

    return false;
}

void Rs7Tuner_ResetAll() {
    for (Key& k : g_keys) {
        k = Key{};
    }

    for (Auto& a : g_auto) {
        a = Auto{};
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
        k.found7 = ReadKey(holder, &k.t7, &raw7, "Rs7String7");
    }
    Key* slot = nullptr;

    for (Key& x : g_keys) {
        if (x.offs == offs) {
            slot = &x;
        }
    }

    if (!slot) {
        for (Key& x : g_keys) {
            if (!slot && !x.offs) {
                slot = &x;
            }
        }
    }

    if (!slot) {
        slot = &g_keys[0];
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

    for (const Key& x : g_keys) {
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
        const uint8_t* guitar = Rd<uint8_t*>(session, kGuitar);
        const uint8_t* info = guitar ? Rd<uint8_t*>(guitar, kGInfo) : nullptr;
        const int published = info ? Rd<uint8_t>(info, 0xB) : 0;
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

    Auto* free = nullptr;

    for (Auto& x : g_auto) {
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
        *free = Auto{session, GetTickCount(), At<int>(session, kCur)};
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
