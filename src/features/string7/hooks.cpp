#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "model/objtable.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/spinlock.h"
#include <windows.h>
#include <cstring>
#include <intrin.h>

// The per-guitar 7-string state behind N5 (Rnd_Guitar_Update): which guitar plays a 7-string arrangement (S4 marks,
// issue #6/#27 visibility rule), the per-frame hand-off to the native strings / frets / pegs / headstock model, the
// SNG string-mask back-fill (N13) and the native string-6 targets of the bend (N18-N23) and pluck (PL1) hooks.
// No Win32 calls, no C++ exception frames and no RAII on the paths a stock song takes: the emulator tests run those
// paths as-is. The table lock is a spinlock on intrinsics; game functions are never called while it is held.

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_fn_alloc = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_strctor = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_shape = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_show = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_detach = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_setpos = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_setrot = 0;
__declspec(dllexport) uintptr_t g_rsxr_frame_global = 0;
__declspec(dllexport) uintptr_t g_rsxr_s7_on = 1;
__declspec(dllexport) volatile long g_rsxr_any7 = 0;
// The native copy of the pluck function (features/strings/hooks_copies.cpp), 0 when not built.
extern __declspec(dllexport) uintptr_t g_rsxr_fncopy_GSPK;
}

namespace {
using namespace rsxrs7;

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

// Per GuitarStrings (registered by the native GuitarStrings, features/strings/hooks.cpp): the song / fade edges N5
// follows.
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
// and its song not over yet (RsxrS7_Visibility).
// A full table replaces its entries in turn.
struct GuitarMode {
    bool seven;
    bool armed;
};

rsxr::ObjTable<const void*, GuitarMode, kMaxGuitars> g_modes;
volatile long g_spin = 0;

void Lock() {
    RsxrSpin_Lock(&g_spin);
}

void Unlock() {
    RsxrSpin_Unlock(&g_spin);
}

Entry* Find(const void* gs) {  // caller holds the lock
    for (Entry& e : g_tab) {
        if (e.gs == gs) {
            return &e;
        }
    }

    return nullptr;
}

// ---- once-per-song log lines ----
volatile long g_logMask = 0, g_logDisarm = 0, g_logBend = 0, g_logEdge = 0;
}  // namespace

// Issue #27: N5's clock (ms). Exported so the emulator tests can replace it (returns 0 there: no
// clock, the time-based rules are skipped).
extern "C" __declspec(dllexport) __declspec(noinline) uint32_t __cdecl Rsxr_S7Now() {
    const uint32_t t = GetTickCount();

    return t ? t : 1;
}

namespace {
using namespace rsxrs7;

__declspec(noinline) void S7LogDisarm(const void* guitar, Disarm why) {
    try {
        RsxrLog("string7: S4 arm of guitar %p ended: %s; S7 from now only while a song runs on it (string 0 +70h)",
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
        RsxrLog("string7: edge guitar %p: string 0 +70h song=%d, GuitarStrings +38h faded-out=%d +39h faded-in=%d, "
                "%u ms since its last frame -> armed=%d, 7-string look %s",
                guitar, song, fadedOut, fadedIn, gapMs, armed ? 1 : 0, want ? "on" : "off");
    } catch (...) {
    }
}

__declspec(noinline) void S7LogBend(uint32_t fret, uint32_t amountBits, const uint8_t* obj) {
    try {
        float amount;

        memcpy(&amount, &amountBits, 4);
        RsxrLog(
            "string7 bend: string-6 bend on the 7th string (fret %u, amount %.3f, string %p; stock SetBend 0x598910; "
            "first this song)",
            fret & 0xFF, amount, obj);
    } catch (...) {
    }
}

void GuitarModeSet(void* guitar, bool seven) {  // caller holds the lock
    if (seven) {
        g_modes.Put(guitar, GuitarMode{true, true});
    } else {
        g_modes.Erase(guitar);
    }
}
}  // namespace

// N13's once-per-song line (exported so the emulator tests can neutralise it).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rsxr_S7LogMask(uint32_t level, int used, int added,
                                                                                  int sections) {
    try {
        RsxrLog("string7 highlight: SNG string mask bit 6 (N12/N13): string 6 used in %d of %d sections at level %u "
                "(%d lit one section early by the back-fill)",
                used, sections, level, added);
    } catch (...) {
    }
}

void RsxrS7_SetGuitarMode(void* guitar, bool seven) {
    if (!guitar) {
        return;
    }

    Lock();
    GuitarModeSet(guitar, seven);
    Unlock();
}

uint8_t* RsxrS7_ObjectOf(void* guitar) {
    if (!g_rsxr_s7_on || !guitar) {
        return nullptr;
    }

    return Rsxr_S7DimTarget(Rsxr_SafeRead<uint8_t*>(static_cast<const uint8_t*>(guitar), rsxr::kGuitarStrings));
}

extern "C" void __cdecl Rsxr_S7MaskBackfill(uint8_t* song, uint32_t level) {
    if (!g_rsxr_s7_on || !song || level >= 32) {
        return;
    }

    uint8_t* b = Rsxr_SafeRead<uint8_t*>(song, 0xF4);
    uint8_t* e = Rsxr_SafeRead<uint8_t*>(song, 0xF8);
    if (!b || e <= b) {
        return;
    }

    const int n = int(uint32_t(e - b) / 0x58u);
    int added = 0;
    int used = RsxrS7_BackfillBit(b, n, level, 0x40, &added);

    int added7 = 0;  // string 7 (the eighth string): bit 7

    used += RsxrS7_BackfillBit(b, n, level, 0x80, &added7);
    added += added7;

    // Stock songs (no string-6 notes) return here: no Win32 call on their path.
    if (used && RsxrLog_Upto(&g_logMask, 1)) {
        Rsxr_S7LogMask(level, used, added, n);
    }
}

extern "C" uint8_t* __cdecl Rsxr_S7DimTarget(void* gs) {
    if (!g_rsxr_s7_on || !gs || !g_rsxr_native_strings || !rsxrgs::IsNative(static_cast<uint8_t*>(gs))) {
        return nullptr;
    }

    return RsxrGs_Active(static_cast<uint8_t*>(gs), 6, false);  // the native string 6 (features/strings/strings.h)
}

// Not inlined into Rsxr_S7BendTarget: the emulator oracle (test_midhook_cold.py) records both stubs' calls of it.
extern "C" __declspec(noinline) uint8_t* __cdecl Rsxr_S7BendObject(void* gs, uint32_t ghost) {
    // N18-N21 extend the stock bound to string 6 of a native GuitarStrings' own arrays.
    if (!gs || !g_rsxr_native_strings || !rsxrgs::IsNative(static_cast<uint8_t*>(gs))) {
        return nullptr;
    }

    return RsxrGs_Active(static_cast<uint8_t*>(gs), 6, ghost != 0);
}

extern "C" uint8_t* __cdecl Rsxr_S7BendTarget(void* gs, uint32_t kind, uint32_t fret, uint32_t amountBits) {
    uint8_t* obj = Rsxr_S7BendObject(gs, 0);

    if (obj && kind == 0 && RsxrLog_Once(&g_logBend)) {
        S7LogBend(fret, amountBits, obj);
    }

    return obj;
}

void RsxrS7_ResetLog() {
    _InterlockedExchange(&g_logMask, 0);
    _InterlockedExchange(&g_logDisarm, 0);
    _InterlockedExchange(&g_logEdge, 0);
    _InterlockedExchange(&g_logBend, 0);
}

extern "C" void __cdecl Rsxr_S7Record(void* gs) {
    if (!g_rsxr_s7_on || !gs) {
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

// C-lite (features/clite/clite.h): the string count is the guitar's build count (InstrumentInfo +0Bh), so the 7th
// string line, the wider fret bars, the 7th peg and the edited headstock model are all decided by it: no S4
// mark, no tuner mark, no song state, nothing to switch back. This is the whole per-frame driver of a native
// guitar. Game thread (N5).
namespace {
void NativeFrame(uint8_t* guitar, uint8_t* gs) {
    uint8_t* info = Rsxr_SafeRead<uint8_t*>(gs, rsxrgs::kInfo);
    const uint8_t n = RsxrGs_InfoNative(info) ? Rsxr_SafeRead<uint8_t>(info, rsxr::kInfoStrings) : 0;
    const bool bass = RsxrGs_InfoNative(info) && rsxr::IsBassKind(Rsxr_SafeRead<uint8_t>(info, rsxr::kInfoKind));
    const bool want = !bass && rsxr::IsExtendedGuitar(n);
    const bool extended = want || (bass && rsxr::IsExtendedBass(n));  // an extended bass: lower floor, edited headstock

    RsxrFrets_Frame(guitar, extended);      // frets.h: F2 (the stock bars of a 6-string guitar are never touched)
    RsxrAsset_HeadFrame(guitar, extended);  // the edited headstock model (assetload.h), before the 7th peg reads
                                            // the (then moved) peg locators
    RsxrHead_Width(guitar, extended);       // [neck] spacing_*: the headstock frame as narrow as the strings
    RsxrHead_Frame(guitar, want);           // the 7th tuning peg (headstock.h)
    RsxrGs_Frame(guitar, gs, want);         // the 7th / 8th string objects (features/strings/strings.h)
}
}  // namespace

extern "C" void __cdecl Rsxr_S7Update(uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    uint8_t* const nativeGs = Rsxr_SafeRead<uint8_t*>(guitar, rsxr::kGuitarStrings);

    if (g_rsxr_native_publish && nativeGs && rsxrgs::IsNative(nativeGs)) {
        NativeFrame(guitar, nativeGs);
        return;
    }

    if (!g_rsxr_s7_on) {
        return;
    }

    uint8_t* gs = nativeGs;
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
    // was not updated for kUnseenMs (RsxrS7_Visibility). The tuner, menus and
    // 6-string/bass songs have no mark, or an arm that ended with the previous song, and no +70h.
    // Per guitar, so another player's 7-string song does not show it here.
    uint8_t* obj0 = Rsxr_SafeRead<uint8_t*>(gs, 4);
    const bool song = Rsxr_SafeRead<uint8_t>(obj0, kDimOn) != 0;
    // The mark itself stays until the next S4 of this guitar overwrites it (a restarted song may not
    // run S4 again); "song running" is re-derived every frame.
    const bool songEnded = e->song && !song;

    e->song = song;
    const int fadedOut = Rsxr_SafeRead<uint8_t>(gs, kGsFadedOut) != 0 ? 1 : 0;
    const int fadedIn = Rsxr_SafeRead<uint8_t>(gs, kGsFadedIn) != 0 ? 1 : 0;
    const bool fadeOutStarted = e->fadedOut == 0 && fadedOut == 1;
    const bool fadeEdge = e->fadedOut != fadedOut || e->fadedIn != fadedIn;

    e->fadedOut = fadedOut;
    e->fadedIn = fadedIn;
    const uint32_t now = Rsxr_S7Now();  // 0 = no clock (emulator tests)
    const uint32_t gapMs = now && e->lastTick ? now - e->lastTick : 0;

    e->lastTick = now;
    GuitarMode* mode = g_modes.Find(guitar);
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
        RsxrS7_Visibility(mode && mode->seven, armed, song, songEnded, fadedOut != 0, fadeOutDone, gapMs >= kUnseenMs);

    if (mode) {
        mode->armed = armed;
    }
    const bool edge =
        mode && mode->seven && (songEnded || fadeEdge || gapMs >= kEdgeGapMs || vis.disarmed != Disarm::None);
    const bool want = vis.want;

    Unlock();
    if (edge && RsxrLog_Upto(&g_logEdge, kEdgeLogCap)) {
        S7LogEdge(guitar, song, fadedOut, fadedIn, gapMs, armed, want);
    }
    if (vis.disarmed != Disarm::None && RsxrLog_Once(&g_logDisarm)) {
        S7LogDisarm(guitar, vis.disarmed);
    }
    // F2: the fret bars follow the same per-guitar 7-string visibility (frets.h).
    RsxrFrets_Frame(guitar, want);
    RsxrAsset_HeadFrame(guitar, want);  // the edited headstock model (assetload.h), before the 7th peg reads
                                        // the (then moved) peg locators
    RsxrHead_Frame(guitar, want);       // the 7th tuning peg (headstock.h)
    // String 6 is a stock object of the native GuitarStrings (features/strings/strings.h); the per-guitar rule above
    // only raises / lowers its active count.
    RsxrGs_Frame(guitar, gs, want);
}

extern "C" int __cdecl Rsxr_S7Pluck(void* gs, uint32_t sngFret, uint32_t fret) {
    return Rsxr_S7PluckString(gs, 6, sngFret, fret);
}

extern "C" int __cdecl Rsxr_S7PluckString(void* gs, uint32_t string, uint32_t sngFret, uint32_t fret) {
    if (!g_rsxr_s7_on || !gs || (string != 6 && string != 7)) {
        return 0;
    }

    if (g_rsxr_native_strings && rsxrgs::IsNative(static_cast<uint8_t*>(gs))) {
        // PL1 (chord strings 6 / 7): the pluck function's entry dispatch (GSPK), i.e. the native copy (checks the
        // active count); asm, as it takes AL / EDX + stack arguments. (`gs` must not be an asm operand: it is the GS
        // register, `push gs` pushed 2Bh.) Only the native copy may run: when the dispatch refuses it answers the
        // stock trampoline, which knows only strings 0-5.
        const void* const strings = gs;
        const uint8_t s = uint8_t(string);
        const uintptr_t pluck = RsxrGs_Target(kGsPluck, static_cast<uint8_t*>(gs), uint32_t(uintptr_t(gs)), fret);

        if (!pluck || pluck != g_rsxr_fncopy_GSPK) {
            return 0;
        }

        __asm {
            push ebx
            push esi
            push edi
            push fret
            push strings
            mov edx, sngFret
            mov al, s
            call pluck
            pop edi
            pop esi
            pop ebx
        }

        return 1;
    }

    return 0;
}

void Rsxr_GuitarGone(void* guitar) {
    if (!guitar) {
        return;
    }

    const uint8_t* g = static_cast<const uint8_t*>(guitar);

    RsxrAsset_ForgetGuitar(g);
    RsxrFrets_Forget(g);
    RsxrPublish_GridForget(g);
    Lock();
    g_modes.Erase(guitar);
    Unlock();
}

extern "C" void __cdecl Rsxr_S7Free(void* gs) {
    if (!gs) {
        return;
    }

    RsxrAsset_ForgetStrings(static_cast<const uint8_t*>(gs));  // the guitar's headstock-model slot (leak audit)
    Lock();

    if (Entry* e = Find(gs)) {
        *e = Entry{};
    }

    Unlock();

    // The guitar owning these strings (GuitarStrings +0) is being destroyed (0x592770 frees the guitar, then its
    // strings): drop its mode, fret bar and headstock-model slots so a later guitar allocated at the same address
    // cannot inherit them and the slots are free again (leaks2).
    void* guitar = nullptr;

    Rsxr_SafeCopyZero(&guitar, gs, sizeof guitar);
    Rsxr_GuitarGone(guitar);
}

void RsxrS7_Stats(RsxrS7Stats* out) {
    RsxrS7Stats st{};

    Lock();

    for (const Entry& e : g_tab) {
        if (e.gs) {
            ++st.guitars;
        }
    }

    for (const auto& m : g_modes) {
        if (m.key && m.value.seven) {
            ++st.sevenGuitars;
        }
    }

    Unlock();
    *out = st;
}
