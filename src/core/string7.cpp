#include "core/string7.h"
#include "core/assetload.h"
#include "core/frets.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/publish.h"
#include "core/string7diag.h"
#define WIN32_LEAN_AND_MEAN
#include "log.h"
#include <windows.h>
#include <cstring>
#include <intrin.h>

// The per-guitar 7-string state behind N5 (Rnd_Guitar_Update): which guitar plays a 7-string arrangement (S4 marks,
// issue #6/#27 visibility rule), the per-frame hand-off to the native strings / frets / pegs / headstock model, the
// SNG string-mask back-fill (N13) and the native string-6 targets of the bend (N18-N23) and pluck (PL1) hooks. The
// DLL-owned 7th string object S7 / S7G of the stage-1 prototype was removed in 1.0 (native GuitarStrings only).
// No Win32 calls, no C++ exception frames and no RAII on the paths a stock song takes: the emulator tests run those
// paths as-is. The table lock is a spinlock on intrinsics; game functions are never called while it is held.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_alloc = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strctor = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_shape = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_show = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_detach = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_setpos = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_setrot = 0;
__declspec(dllexport) uintptr_t g_rs7_frame_global = 0;
__declspec(dllexport) uintptr_t g_rs7_s7_on = 1;
__declspec(dllexport) volatile long g_rs7_any7 = 0;
__declspec(dllexport) float g_rs7_s7_v = rs7s7::kDefaultAtlasRow;
}

extern "C" void Rs7_Stub_GSPK();

namespace {
using namespace rs7s7;

constexpr uint32_t kDimOn = 0x70;  // string object byte: section dimming active (song running; 0x7DFB50 / 0x7E0040)
// GuitarStrings: +38h "faded out" / +39h "faded in" (set by Rnd_GuitarStrings_FadeOut 0x59A720 /
// FadeIn 0x59A820 when they start the strings' ClipFade, each clears the other).
constexpr uint32_t kGsFadedOut = 0x38;
constexpr uint32_t kGsFadedIn = 0x39;
// Issue #27: N5 runs every frame the guitar scene is up; a gap this long means it was gone (results,
// menus, a reused guitar object) and ends an S4 arm while no song runs. Gaps from kEdgeGapMs are logged.
constexpr uint32_t kUnseenMs = 1500;
constexpr uint32_t kEdgeGapMs = 250;
constexpr long kEdgeLogCap = 40;

// Per GuitarStrings (registered by the native GuitarStrings, gsnative.cpp): the song / fade edges N5 follows.
struct Entry {
    uint8_t* gs;           // GuitarStrings* (key; nullptr = free slot)
    bool song;             // stock string 0's +70h last frame (song running on this guitar)
    int fadedOut;          // GuitarStrings +38h last frame (-1 = not seen yet): its rise ends an S4 arm
    int fadedIn;           // GuitarStrings +39h last frame (-1 = not seen yet; edge log only)
    uint32_t lastTick;     // GetTickCount of the last N5 frame (0 = none yet): a gap ends an S4 arm (#27)
    uint32_t fadeOutTick;  // GetTickCount when +38h last rose (0 = not fading out / faded out)
};

Entry g_tab[kMaxGuitars];

// S4: which guitars (Rnd_Guitar*, session+37Ch) play a 7-string arrangement; armed = set up for it
// and its song not over yet (Rs7S7_Visibility).
struct GuitarMode {
    void* guitar;
    bool seven;
    bool armed;
};

GuitarMode g_modes[kMaxGuitars];
volatile long g_spin = 0;

void Lock() {
    while (_InterlockedCompareExchange(&g_spin, 1, 0) != 0) {
        _mm_pause();
    }
}

void Unlock() {
    _InterlockedExchange(&g_spin, 0);
}

Entry* Find(const void* gs) {  // caller holds the lock
    for (Entry& e : g_tab) {
        if (e.gs == gs) {
            return &e;
        }
    }

    return nullptr;
}

template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

// ---- once-per-song log lines ----
volatile long g_logMask = 0, g_logDisarm = 0, g_logBend = 0, g_logEdge = 0, g_logGhostBend = 0;

bool Once(volatile long* flag) {
    return _InterlockedExchange(flag, 1) == 0;
}

// At most `cap` lines per song for events that may legitimately differ per guitar.
bool Upto(volatile long* counter, long cap) {
    return _InterlockedIncrement(counter) <= cap;
}

}  // namespace

// Issue #27: N5's clock (ms). Exported so the emulator tests can replace it (returns 0 there: no
// clock, the time-based rules are skipped).
extern "C" __declspec(dllexport) __declspec(noinline) uint32_t __cdecl Rs7_S7Now() {
    const uint32_t t = GetTickCount();

    return t ? t : 1;
}

namespace {
using namespace rs7s7;

__declspec(noinline) void S7LogDisarm(const void* guitar, Disarm why) {
    try {
        Rs7Log("string7: S4 arm of guitar %p ended: %s; S7 from now only while a song runs on it (string 0 +70h)",
               guitar,
               why == Disarm::Unseen ? "the guitar was not updated (its scene was gone) and no song runs"
                                     : "the stock strings faded out while no song runs (GuitarStrings +38h "
                                       "rose and the fade finished, or was set when the song ended)");
    } catch (...) {
    }
}

// Issue #27: the song / stock-string fade edges of a 7-string guitar (capped per song), so each exit
// path shows in the log which signal ended the 7-string look.
__declspec(noinline) void S7LogEdge(const void* guitar, int song, int fadedOut, int fadedIn, uint32_t gapMs, bool armed,
                                    bool want) {
    try {
        Rs7Log("string7: edge guitar %p: string 0 +70h song=%d, GuitarStrings +38h faded-out=%d +39h faded-in=%d, "
               "%u ms since its last frame -> armed=%d, 7-string look %s",
               guitar, song, fadedOut, fadedIn, gapMs, armed ? 1 : 0, want ? "on" : "off");
    } catch (...) {
    }
}

__declspec(noinline) void S7LogBend(uint32_t fret, uint32_t amountBits, const uint8_t* obj) {
    try {
        float amount;

        memcpy(&amount, &amountBits, 4);
        Rs7Log(
            "string7 bend: string-6 bend on the 7th string (fret %u, amount %.3f, string %p; stock SetBend 0x598910; "
            "first this song)",
            fret & 0xFF, amount, obj);
    } catch (...) {
    }
}

__declspec(noinline) void S7LogGhostBend(uint32_t fret, uint32_t amountBits, const uint8_t* ghost) {
    try {
        float amount;

        memcpy(&amount, &amountBits, 4);
        Rs7Log("string7 ghost: string-6 chart bend on the 7th string's ghost (fret %u, chart amount %.3f, ghost %p; "
               "stock SetBend 0x598910 ghost_up; first this song)",
               fret & 0xFF, amount, ghost);
    } catch (...) {
    }
}

void GuitarModeSet(void* guitar, bool seven) {  // caller holds the lock
    GuitarMode* m = nullptr;

    for (GuitarMode& x : g_modes) {
        if (x.guitar == guitar) {
            m = &x;
        }
    }

    if (!m && seven) {
        for (GuitarMode& x : g_modes) {
            if (!m && !x.guitar) {
                m = &x;
            }
        }

        if (!m) {
            m = &g_modes[0];
        }
    }

    if (m) {
        *m = seven ? GuitarMode{guitar, true, true} : GuitarMode{};
    }
}

GuitarMode* GuitarFind(const void* guitar) {  // caller holds the lock
    for (GuitarMode& x : g_modes) {
        if (x.guitar == guitar) {
            return &x;
        }
    }

    return nullptr;
}
}  // namespace

// N13's once-per-song line (exported so the emulator tests can neutralise it).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rs7_S7LogMask(uint32_t level, int used, int added,
                                                                                 int sections) {
    try {
        Rs7Log("string7 highlight: SNG string mask bit 6 (N12/N13): string 6 used in %d of %d sections at level %u "
               "(%d lit one section early by the back-fill)",
               used, sections, level, added);
    } catch (...) {
    }
}

void Rs7S7_DimColour(const float rgb[3], float dimValue, float out[3]) {
    const float m = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
    const float k = m > 0.0f ? dimValue / m : 0.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = rgb[i] * k;
    }
}

void Rs7S7_GlowColour(const float rgb[3], float out[3]) {
    const float m = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
    const float k = m > 0.0f ? 1.0f / m : 0.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = rgb[i] * k;
    }
}

int Rs7S7_BackfillBit(uint8_t* sections, int count, uint32_t level, uint8_t bit, int* added) {
    int lit = 0;

    if (added) {
        *added = 0;
    }
    if (!sections || count <= 0 || level >= 32) {
        return 0;
    }

    for (int i = count - 1; i > 0; --i) {
        uint8_t& cur = sections[i * 0x58 + 0x34 + level];
        uint8_t& prev = sections[(i - 1) * 0x58 + 0x34 + level];

        if ((cur & bit) && !(prev & bit)) {
            prev = uint8_t(prev | bit);
            if (added) {
                *added += 1;
            }
            --i;
        }
    }

    for (int i = 0; i < count; ++i) {
        lit += (sections[i * 0x58 + 0x34 + level] & bit) ? 1 : 0;
    }

    return lit;
}

uint32_t rs7s7::g_fadeHoldMs = 1500;

rs7s7::Visibility rs7s7::Rs7S7_Visibility(bool seven, bool& armed, bool song, bool songEnded, bool fadedOut,
                                          bool fadeOutDone, bool unseen) {
    Disarm why = Disarm::None;

    if (armed && !song) {
        if ((fadedOut && fadeOutDone) || (songEnded && fadedOut)) {
            why = Disarm::FadedOut;
        } else if (unseen) {
            why = Disarm::Unseen;
        }
    }

    if (why != Disarm::None) {
        armed = false;
    }

    return Visibility{seven && (armed || song), why};
}

void Rs7S7_SetGuitarMode(void* guitar, bool seven) {
    if (!guitar) {
        return;
    }

    Lock();
    GuitarModeSet(guitar, seven);
    Unlock();
}

uint8_t* Rs7S7_ObjectOf(void* guitar) {
    if (!g_rs7_s7_on || !guitar) {
        return nullptr;
    }

    return Rs7_S7DimTarget(At<uint8_t*>(static_cast<uint8_t*>(guitar), 0xC));
}

extern "C" void __cdecl Rs7_S7MaskBackfill(uint8_t* song, uint32_t level) {
    if (!g_rs7_s7_on || !song || level >= 32) {
        return;
    }

    uint8_t* b = At<uint8_t*>(song, 0xF4);
    uint8_t* e = At<uint8_t*>(song, 0xF8);
    if (!b || e <= b) {
        return;
    }

    const int n = int(uint32_t(e - b) / 0x58u);
    int added = 0;
    const int used = Rs7S7_BackfillBit(b, n, level, 0x40, &added);

    // Stock songs (no string-6 notes) return here: no Win32 call on their path.
    if (used && Upto(&g_logMask, 1)) {
        Rs7_S7LogMask(level, used, added, n);
    }
}

extern "C" uint8_t* __cdecl Rs7_S7DimTarget(void* gs) {
    if (!g_rs7_s7_on || !gs || !g_rs7_native_strings || !rs7gs::IsNative(static_cast<uint8_t*>(gs))) {
        return nullptr;
    }

    return Rs7Gs_Active(static_cast<uint8_t*>(gs), 6, false);  // the native string 6 (gsnative.h)
}

extern "C" uint8_t* __cdecl Rs7_S7BendTarget(void* gs, uint32_t kind, uint32_t fret, uint32_t amountBits) {
    // N18-N23 extend the stock bound to string 6 of a native GuitarStrings' own arrays.
    if (!gs || !g_rs7_native_strings || !rs7gs::IsNative(static_cast<uint8_t*>(gs))) {
        return nullptr;
    }

    uint8_t* obj = Rs7Gs_Active(static_cast<uint8_t*>(gs), 6, kind >= 2);

    if (obj && kind == 2 && Once(&g_logGhostBend)) {
        S7LogGhostBend(fret, amountBits, obj);
    } else if (obj && kind == 0 && Once(&g_logBend)) {
        S7LogBend(fret, amountBits, obj);
    }

    return obj;
}

void Rs7S7_ResetLog() {
    _InterlockedExchange(&g_logMask, 0);
    _InterlockedExchange(&g_logDisarm, 0);
    _InterlockedExchange(&g_logEdge, 0);
    _InterlockedExchange(&g_logBend, 0);
    _InterlockedExchange(&g_logGhostBend, 0);
}

extern "C" void __cdecl Rs7_S7Record(void* gs) {
    if (!g_rs7_s7_on || !gs) {
        return;
    }

    Lock();
    Entry* e = Find(gs);

    if (!e) {
        e = Find(nullptr);
    }

    if (e && e->gs != gs) {
        *e = Entry{static_cast<uint8_t*>(gs)};
        e->fadedOut = -1;
        e->fadedIn = -1;
    }

    Unlock();
}

// C-lite (core/clite.h): the string count is the guitar's build count (InstrumentInfo +0Bh), so the 7th
// string line, the wider fret bars, the 7th peg and the edited headstock model are all decided by it: no S4
// mark, no tuner mark, no song state, nothing to switch back. This is the whole per-frame driver of a native
// guitar. Game thread (N5).
namespace {
void NativeFrame(uint8_t* guitar, uint8_t* gs) {
    uint8_t* info = At<uint8_t*>(gs, rs7gs::kInfo);
    const bool want = Rs7Gs_InfoNative(info) && (At<uint8_t>(info, 0x0B) == 7 || At<uint8_t>(info, 0x0B) == 8);

    Rs7Frets_Frame(guitar, want);      // frets.h: F2 (the stock bars of a 6-string guitar are never touched)
    Rs7Asset_HeadFrame(guitar, want);  // the edited headstock model (assetload.h), before the 7th peg reads
                                       // the (then moved) peg locators
    Rs7Head_Frame(guitar, want);       // the 7th tuning peg (headstock.h)
    Rs7Gs_Frame(guitar, gs, want);     // the 7th / 8th string objects (gsnative.h)
}
}  // namespace

extern "C" void __cdecl Rs7_S7Update(uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    uint8_t* const nativeGs = At<uint8_t*>(guitar, 0xC);

    if (g_rs7_native_publish && nativeGs && rs7gs::IsNative(nativeGs)) {
        NativeFrame(guitar, nativeGs);
        return;
    }

    if (!g_rs7_s7_on) {
        return;
    }

    uint8_t* gs = At<uint8_t*>(guitar, 0xC);
    if (!gs) {
        return;
    }

    Lock();
    Entry* e = Find(gs);

    if (!e) {
        Unlock();
        return;
    }

    // The 7-string look is on while this guitar is set up for a 7-string arrangement (S4 marked and armed it: from
    // song load, so string 6 is there for the stock intro fade-in, issue #6) or its song runs (stock string 0's
    // +70h: set at song start 0x7DFB50, cleared at the song end 0x7E0040). The arm outlives the song
    // (outro, issue #27) and ends, while no song runs, when the stock strings fade out or the guitar
    // was not updated for kUnseenMs (Rs7S7_Visibility). The tuner, menus and
    // 6-string/bass songs have no mark, or an arm that ended with the previous song, and no +70h.
    // Per guitar, so another player's 7-string song does not show it here.
    uint8_t* obj0 = At<uint8_t*>(gs, 4);
    const bool song = obj0 && At<uint8_t>(obj0, kDimOn) != 0;
    // The mark itself stays until the next S4 of this guitar overwrites it (a restarted song may not
    // run S4 again); "song running" is re-derived every frame.
    const bool songEnded = e->song && !song;

    e->song = song;
    const int fadedOut = At<uint8_t>(gs, kGsFadedOut) != 0 ? 1 : 0;
    const int fadedIn = At<uint8_t>(gs, kGsFadedIn) != 0 ? 1 : 0;
    const bool fadeOutStarted = e->fadedOut == 0 && fadedOut == 1;
    const bool fadeEdge = e->fadedOut != fadedOut || e->fadedIn != fadedIn;

    e->fadedOut = fadedOut;
    e->fadedIn = fadedIn;
    const uint32_t now = Rs7_S7Now();  // 0 = no clock (emulator tests)
    const uint32_t gapMs = now && e->lastTick ? now - e->lastTick : 0;

    e->lastTick = now;
    GuitarMode* mode = GuitarFind(guitar);
    bool armed = mode && mode->armed;

    // Only a fade-out that starts while armed counts (a menu's +38h left set before S4 does not).
    if (fadeOutStarted && armed) {
        e->fadeOutTick = now ? now : 1;  // (no clock: the hold time never runs out)
    }
    if (!fadedOut || !armed) {
        e->fadeOutTick = 0;
    }
    const uint32_t fadeOutMs = e->fadeOutTick && now ? now - e->fadeOutTick : 0;
    const bool fadeOutDone = e->fadeOutTick && fadeOutMs >= g_fadeHoldMs;
    const Visibility vis =
        Rs7S7_Visibility(mode && mode->seven, armed, song, songEnded, fadedOut != 0, fadeOutDone, gapMs >= kUnseenMs);

    if (mode) {
        mode->armed = armed;
    }
    const bool edge =
        mode && mode->seven && (songEnded || fadeEdge || gapMs >= kEdgeGapMs || vis.disarmed != Disarm::None);
    const bool want = vis.want;

    Unlock();
    if (edge && Upto(&g_logEdge, kEdgeLogCap)) {
        S7LogEdge(guitar, song, fadedOut, fadedIn, gapMs, armed, want);
    }
    if (vis.disarmed != Disarm::None && Once(&g_logDisarm)) {
        S7LogDisarm(guitar, vis.disarmed);
    }
    // F2: the fret bars follow the same per-guitar 7-string visibility (frets.h).
    Rs7Frets_Frame(guitar, want);
    Rs7Asset_HeadFrame(guitar, want);  // the edited headstock model (assetload.h), before the 7th peg reads
                                       // the (then moved) peg locators
    Rs7Head_Frame(guitar, want);       // the 7th tuning peg (headstock.h)
    // String 6 is a stock object of the native GuitarStrings (gsnative.h); the per-guitar rule above only raises /
    // lowers its active count.
    Rs7Gs_Frame(guitar, gs, want);
}

extern "C" int __cdecl Rs7_S7Pluck(void* gs, uint32_t sngFret, uint32_t fret) {
    if (!g_rs7_s7_on || !gs) {
        return 0;
    }

    if (g_rs7_native_strings && rs7gs::IsNative(static_cast<uint8_t*>(gs))) {
        // PL1 (chord string 6): the stock pluck entry, i.e. the native copy (it checks the active count).
        // (`gs` must not be an asm operand: it is the GS segment register, `push gs` pushed 2Bh.)
        const void* const strings = gs;

        __asm {
            push ebx
            push esi
            push edi
            push fret
            push strings
            mov edx, sngFret
            mov al, 6
            call Rs7_Stub_GSPK
            pop edi
            pop esi
            pop ebx
        }

        return 1;
    }

    return 0;
}

void Rs7_GuitarGone(void* guitar) {
    if (!guitar) {
        return;
    }

    const uint8_t* g = static_cast<const uint8_t*>(guitar);

    Rs7Asset_ForgetGuitar(g);
    Rs7Frets_Forget(g);
    Lock();

    for (GuitarMode& m : g_modes) {
        if (m.guitar == guitar) {
            m = GuitarMode{};
        }
    }

    Unlock();
}

extern "C" void __cdecl Rs7_S7Free(void* gs) {
    if (!gs) {
        return;
    }

    Rs7Asset_ForgetStrings(static_cast<const uint8_t*>(gs));  // the guitar's headstock-model slot (leak audit)
    Lock();
    Entry* e = Find(gs);
    // The guitar owning these strings (GuitarStrings +0) is being destroyed (0x592770 frees the
    // guitar, then its strings): drop its song/armed entry so a later guitar allocated at the
    // same address cannot inherit it and the slot is free again (leaks2). Only reads the strings
    // object when some entry is in use: with none (6-string/bass sessions) this stays the stock path.
    bool anyMode = false;

    for (const GuitarMode& m : g_modes) {
        anyMode = anyMode || m.guitar;
    }

    void* guitar = nullptr;

    rs7s7d::SafeCopy(&guitar, gs, sizeof guitar);

    if (guitar) {
        Rs7Frets_Forget(static_cast<const uint8_t*>(guitar));        // its fret bar state slot
        Rs7Asset_ForgetGuitar(static_cast<const uint8_t*>(guitar));  // (ForgetStrings above, by address too)
    }

    if (anyMode && guitar) {
        for (GuitarMode& m : g_modes) {
            if (m.guitar == guitar) {
                m = GuitarMode{};
            }
        }
    }

    if (e) {
        *e = Entry{};
    }
    Unlock();
}

void Rs7S7_Stats(Rs7S7Stats* out) {
    Rs7S7Stats st{};

    Lock();

    for (const Entry& e : g_tab) {
        if (e.gs) {
            ++st.guitars;
        }
    }

    for (const GuitarMode& m : g_modes) {
        if (m.guitar && m.seven) {
            ++st.sevenGuitars;
        }
    }

    Unlock();
    *out = st;
}
