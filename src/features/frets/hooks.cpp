// The fret bars' game glue (frets.h): F1 / F2 re-place and scale the bars of a guitar built with 7/8 strings, the
// highway floor, the IN1 inlays and the chord-frame geometry B2 reads. The logic it calls is frets.cpp, its log
// lines are hooks_log.cpp.
#include "features/frets/hooks.h"
#include "features/frets/state.h"
#include "features/scene/scene.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include "platform/spinlock.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <iterator>

// No Win32 calls, no C++ exception frames on the path a stock song takes (no guitar wants 7
// strings): the emulator test runs F1 that way. Game functions are never called while the table
// lock is held. Logging sits in noinline helpers reached only on 7-string events or once per song.

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_fn_setscale = 0;
__declspec(dllexport) uintptr_t g_rsxr_frets_on = 1;
__declspec(dllexport) float g_rsxr_fret_halflen = rsxrfrets::kDefaultHalfLen;
}

namespace rsxrfretglue {
State g_state;

using namespace rsxrfrets;

uint8_t* Bar(uint8_t* grid, int f) {
    return grid + kBars + uint32_t(f) * kBarStride;
}

int BarCount(uint8_t* guitar) {
    const uint8_t* info = rsxr::GuitarInfo(guitar);
    if (!info) {
        return 0;
    }

    const int n = info[rsxr::kInfoFretBars];

    return n > kMaxBars ? kMaxBars : n;
}

bool Attach(uint8_t* guitar, const float** a0, const float** a1, const float** w0, const float** w1) {
    uint8_t* gs = UncheckedField<uint8_t*>(guitar, kGuitarStrings);
    uint8_t* o0 = gs ? UncheckedField<uint8_t*>(gs, 4) : nullptr;
    uint8_t* o1 = gs ? UncheckedField<uint8_t*>(gs, 8) : nullptr;
    if (!o0 || !o1) {
        return false;
    }

    *a0 = reinterpret_cast<const float*>(o0 + 0x08);
    *a1 = reinterpret_cast<const float*>(o1 + 0x08);
    if (w0) {
        *w0 = reinterpret_cast<const float*>(o0 + 0x14);
    }
    if (w1) {
        *w1 = reinterpret_cast<const float*>(o1 + 0x14);
    }

    return true;
}

// The bars are laid out for the string count the guitar was built with (InstrumentInfo +0Bh), which never changes
// during the guitar's life. The lanes below the stock low string: 1 on a 7-string, 2 on an 8-string guitar, 1 on a
// bass built with 5 / 6 strings (the low B), else 0. A 6-string bass's high C lies beyond the G at the other end
// (FarLanes).
int ExtraLanes(const uint8_t* guitar) {
    const uint8_t* info = rsxr::GuitarInfo(guitar);

    if (!info) {
        return 0;
    }

    const unsigned count = info[rsxr::kInfoStrings];
    if (rsxr::IsBassKind(info[rsxr::kInfoKind])) {
        return rsxr::IsExtendedBass(count) ? 1 : 0;
    }

    return rsxr::IsExtendedGuitar(count) ? int(count) - 6 : 0;
}

// A bass's stock far string is the G (3), a guitar's string 5; a 6-string bass has its high C one lane beyond it.
int StockFar(const uint8_t* guitar) {
    const uint8_t* info = rsxr::GuitarInfo(guitar);

    return info && rsxr::IsBassKind(info[rsxr::kInfoKind]) ? 3 : 5;
}

int FarLanes(const uint8_t* guitar) {
    const uint8_t* info = rsxr::GuitarInfo(guitar);

    return info && rsxr::IsBassKind(info[rsxr::kInfoKind]) && info[rsxr::kInfoStrings] == 6 ? 1 : 0;
}

int FloorOffset(uint8_t* guitar, uint8_t* grid, float out[3]) {
    if (!g_rsxr_frets_on || !guitar) {
        return 0;
    }

    __try {
        const int lanes = ExtraLanes(guitar);
        if (!lanes) {
            return 0;
        }

        const float *a0, *a1;
        if (!grid || !Attach(guitar, &a0, &a1, nullptr, nullptr)) {
            return -1;
        }

        uint8_t* bar0 = Bar(grid, 0);
        uint8_t live = 0;
        float p0[3], p1[3], floor[3];
        if (!Rsxr_SafeCopyZero(&live, bar0, sizeof live) || !live || !Rsxr_SafeCopyZero(p0, a0, sizeof p0) ||
            !Rsxr_SafeCopyZero(p1, a1, sizeof p1) || !Rsxr_SafeCopyZero(floor, bar0 + kBarBLocal, sizeof floor)) {
            return -1;
        }

        return FloorMove(p0, p1, floor, lanes, StockFar(guitar), FarLanes(guitar), out);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return -1;
    }
}

void GameSetScale(uint8_t* entity, float k) {  // Entity_SetScale 0x7AE2C0: EAX = entity, cdecl(float)
    __asm {
        push ebx
        push esi
        push edi
        push k
        mov eax, entity
        call dword ptr [g_rsxr_fn_setscale]
        add esp, 4
        pop edi
        pop esi
        pop ebx                    // ebx / esi / edi are kept across the game's setter
    }
}
}  // namespace rsxrfretglue

void RsxrFrets_SetScale(uint8_t* entity, float k) {
    if (entity && g_rsxr_fn_setscale) {
        rsxrfretglue::GameSetScale(entity, k);
    }
}

namespace {
using namespace rsxrfrets;
using namespace rsxrfretglue;

void Lock() {
    RsxrSpin_Lock(&g_state.spin);
}

void Unlock() {
    RsxrSpin_Unlock(&g_state.spin);
}

GuitarState* FindGuitar(const void* guitar) {  // caller holds the lock
    return g_state.guitars.Find(guitar);
}

GuitarState* FindOrAdd(uint8_t* guitar) {  // caller holds the lock
    if (GuitarState* s = FindGuitar(guitar)) {
        return s;
    }

    return &g_state.guitars.Put(guitar, GuitarState{false, -1, 0.0f, 0});
}

bool WantsSeven(const uint8_t* guitar) {
    return ExtraLanes(guitar) > 0;
}

// ---- game calls ----
void GameSetPos(uint8_t* entity, const float* v) {  // Entity_SetTranslation 0x7ADF70 cdecl(entity, &vec3)
    using Fn = void(__cdecl*)(uint8_t*, const float*);

    reinterpret_cast<Fn>(g_rsxr_fn_setpos)(entity, v);
}

void Midpoint(uint8_t* bar, float out[3]) {  // the stock translation of 0x58FB20 (0x58FBE0..0x58FC3F)
    const float* a = reinterpret_cast<const float*>(bar + kBarAWorld);
    const float* b = reinterpret_cast<const float*>(bar + kBarBWorld);

    for (int i = 0; i < 3; ++i) {
        out[i] = (a[i] + b[i]) * 0.5f;
    }
}

// Each of the first `bars` bars that has an entity: its stock midpoint + `shift`, uniform `scale`. The first bar's
// stock and new midpoint go to mid0 / new0 when given. Returns the bars set.
int PlaceBars(uint8_t* grid, int bars, const float shift[3], float scale, float* mid0, float* new0) {
    int set = 0;

    for (int f = 0; f < bars; ++f) {
        uint8_t* bar = Bar(grid, f);
        uint8_t* entity = UncheckedField<uint8_t*>(bar, kBarEntity);
        if (!entity) {
            continue;
        }

        float p[3];

        Midpoint(bar, p);
        if (f == 0 && mid0) {
            std::memcpy(mid0, p, sizeof p);
        }

        for (int i = 0; i < 3; ++i) {
            p[i] += shift[i];
        }

        if (f == 0 && new0) {
            std::memcpy(new0, p, sizeof p);
        }
        GameSetPos(entity, p);
        GameSetScale(entity, scale);
        ++set;
    }

    return set;
}

// Re-place and scale every bar of `guitar` (F1 right after the stock body, F2 on a change). Returns
// the number of bars set; 0 = nothing to do yet (no bars / strings loaded).
int Apply(uint8_t* guitar, uint8_t* grid, const char* by, bool f1) {
    if (!g_rsxr_fn_setpos || !g_rsxr_fn_setscale || !grid) {
        return 0;
    }

    const float *a0, *a1;
    Extend x;

    if (!Attach(guitar, &a0, &a1, nullptr, nullptr)) {
        if (RsxrLog_Once(&g_state.logFail)) {
            LogFail("guitar strings not loaded (no attach points)", guitar);
        }
        return 0;
    }

    uint8_t* bar0 = Bar(grid, 0);
    float m0[3];

    for (int i = 0; i < 3; ++i) {
        m0[i] = 0.5f * (reinterpret_cast<const float*>(bar0 + kBarALocal)[i] +
                        reinterpret_cast<const float*>(bar0 + kBarBLocal)[i]);
    }

    // [neck] spacing_*: the strings were scaled by k about the string-axis origin (features/spacing); the stock bar
    // mesh was not. Scale its midpoint and half length the same way first, so its margin beyond the outer strings
    // stays the stock one, and fold k into the result.
    const float k = RsxrSpacing_Factor(rsxr::GuitarInfo(guitar));
    const float halfLen = g_rsxr_fret_halflen * k;
    float shift0[3] = {};

    if (k != 1.0f) {
        rsxrfrets::ScaleBarMid(a0, a1, k, m0, shift0);
    }

    if (!Compute(a0, a1, m0, halfLen, ExtraLanes(guitar), &x, StockFar(guitar), FarLanes(guitar))) {
        if (RsxrLog_Once(&g_state.logFail)) {
            LogFail("degenerate string spacing", guitar);
        }
        return 0;
    }

    x.scale *= k;

    for (int i = 0; i < 3; ++i) {
        x.shift[i] += shift0[i];
    }

    const int bars = BarCount(guitar);
    float mid0[3] = {}, new0[3] = {};
    const int set = PlaceBars(grid, bars, x.shift, x.scale, mid0, new0);

    if (set && RsxrLog_Once(f1 ? &g_state.logF1 : &g_state.logApply)) {
        LogApply(by, guitar, grid, bars, set, x, mid0, new0);
    }

    return set;
}

int Restore(uint8_t* guitar, uint8_t* grid) {
    if (!g_rsxr_fn_setpos || !g_rsxr_fn_setscale || !grid) {
        return 0;
    }

    constexpr float kNoShift[3] = {};

    return PlaceBars(grid, BarCount(guitar), kNoShift, 1.0f, nullptr, nullptr);
}

// Apply / Restore for the per-frame driver (N5), SEH-guarded like FRG1 / FRG2: the guitar's grid, bars and attach
// points are game memory; a fault counts as no bar set.
int GuardedApply(uint8_t* guitar, uint8_t* grid, const char* by) {
    __try {
        return Apply(guitar, grid, by, false);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

int GuardedRestore(uint8_t* guitar, uint8_t* grid) {
    __try {
        return Restore(guitar, grid);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

// The floor (frets.h): 0x58FB20 has just written every bar's world ends from the locators (+the guitar offset);
// they stay as set here until its next run, so every reader this frame sees one floor.
int ShiftFloor(uint8_t* guitar, uint8_t* grid) {
    float shift[3];
    const int moved = FloorOffset(guitar, grid, shift);
    if (moved <= 0) {
        return moved;
    }

    int n = BarCount(guitar) + 1;  // the time axis / fret numbers read records 0..count

    if (n > kMaxBars) {
        n = kMaxBars;
    }
    float first[3];
    uint8_t* bar0 = Bar(grid, 0);

    std::memcpy(first, bar0 + kBarBWorld, sizeof first);

    for (int f = 0; f < n; ++f) {
        float* b = reinterpret_cast<float*>(Bar(grid, f) + kBarBWorld);

        for (int i = 0; i < 3; ++i) {
            b[i] += shift[i];
        }
    }

    if (RsxrLog_Once(&g_state.logFloor)) {
        LogFloor(guitar, first, shift, n);
    }

    return moved;
}

void RecoverFloor(uint8_t* guitar, uint8_t* grid) {
    int moved = -1;

    __try {
        moved = ShiftFloor(guitar, grid);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }

    Lock();
    if (GuitarState* s = FindGuitar(guitar)) {
        s->floorMoved = moved;
    }
    Unlock();
}
}  // namespace

extern "C" void __cdecl Rsxr_InlaysAfter(uint8_t* guitar, uint8_t* inlays) {
    if (!g_rsxr_frets_on || !inlays || !WantsSeven(guitar) || !g_rsxr_fn_setpos) {
        return;
    }

    uint8_t* entity = UncheckedField<uint8_t*>(inlays, 4);
    uint8_t* head = UncheckedField<uint8_t*>(guitar, rsxr::kGuitarHead);
    uint8_t* frame = head ? UncheckedField<uint8_t*>(head, 4) : nullptr;
    uint8_t* froot = frame ? rsxrs7d::EntityRoot(frame) : nullptr;
    const float *a0, *a1;
    if (!entity || !froot || !Attach(guitar, &a0, &a1, nullptr, nullptr)) {
        return;
    }

    float p[3];
    // The frame's placement, which 0x59BA80 gives the inlay entity (its root's world translation: (1,1,1) in a song,
    // (0,0,-1000) in the tuner, the frame root has no parent transform).
    if (!Rsxr_SafeCopyZero(p, froot + 0xA4, sizeof p)) {
        return;
    }

    // The centre moves half a spacing per added low lane and back half a spacing per added far lane (a 6-string
    // bass: low B and high C, centre unchanged; an 8-string: one full spacing).
    const int net = ExtraLanes(guitar) - FarLanes(guitar);
    const float k = 0.5f * float(net);

    for (int i = 0; i < 3; ++i) {
        p[i] += k * (a0[i] - a1[i]);
    }

    GameSetPos(entity, p);
    if (RsxrLog_Once(&g_state.logInlay)) {
        LogInlay(guitar, p, k * (a0[2] - a1[2]), net);
    }
}

namespace {
// F1's Apply + ShiftFloor, SEH-guarded like GuardedApply (game memory; a fault counts as no bar set).
int GuardedF1(uint8_t* guitar, uint8_t* grid) {
    __try {
        const int set = Apply(guitar, grid, "F1 (after Rnd_FretBars_ApplyTransforms 0x58FB20)", true);

        RecoverFloor(guitar, grid);
        return set;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}
}  // namespace

extern "C" void __cdecl Rsxr_FretsAfterApply(uint8_t* grid) {
    if (!grid) {
        return;
    }

    uint8_t* guitar = UncheckedField<uint8_t*>(grid, 0);  // 0x58FB2B: [grid] = the Rnd_Guitar (+24h info)
    if (!WantsSeven(guitar)) {
        return;  // every guitar built with 6 strings / bass
    }

    // The stock transform just replaced the world ends, even if the earlier apply had succeeded.
    Lock();
    GuitarState* reset = FindOrAdd(guitar);

    if (g_rsxr_frets_on) {
        reset->applied = false;
    }
    reset->floorMoved = -1;
    Unlock();
    if (!g_rsxr_frets_on) {
        return;
    }

    const int set = GuardedF1(guitar, grid);
    if (!set) {
        return;
    }

    Lock();
    GuitarState* s = FindOrAdd(guitar);

    s->applied = true;
    s->halfLen = g_rsxr_fret_halflen;
    if (!s->verify) {
        s->verify = kVerifyFrames;
    }
    Unlock();
}

void RsxrFrets_Frame(uint8_t* guitar, bool want) {
    if (!guitar) {
        return;
    }

    // fret_extend is live (overlay): switched off, an applied guitar is restored below and nothing
    // else is touched; a guitar without a GuitarState returns here as before.
    const bool on = g_rsxr_frets_on != 0;

    want = want && on;
    uint8_t* grid = UncheckedField<uint8_t*>(guitar, kGuitarGrid);

    Lock();
    GuitarState* s = FindGuitar(guitar);

    if (!on && !(s && s->applied)) {
        Unlock();
        return;
    }

    if (!s && want) {
        s = FindOrAdd(guitar);
    }

    if (!s) {
        Unlock();
        return;  // a guitar built with 6 strings / bass: nothing to do
    }

    // fret_halflen changed while applied (overlay): apply again (Apply starts from the stock midpoints).
    const bool reapply = s->applied && want && s->halfLen != g_rsxr_fret_halflen;
    const bool applied = s->applied && !reapply;
    const bool recoverFloor = want && s->floorMoved < 0;

    int verify = 0;

    if (s->verify > 0) {
        verify = --s->verify == 0 ? 1 : 0;
    }
    Unlock();

    if (want && !applied) {
        // F1 did not (or could not yet: no attach points) place the bars after the guitar's last ApplyTransforms.
        const int set =
            GuardedApply(guitar, grid, reapply ? "F2 (fret_halflen changed)" : "F2 (N5: guitar built with 7 strings)");

        if (reapply) {
            RsxrLog("frets: %d bars re-applied with fret_halflen=%.3f (overlay)", set, double(g_rsxr_fret_halflen));
        }

        if (set) {
            Lock();

            if (GuitarState* f = FindGuitar(guitar)) {
                f->applied = true;
                f->halfLen = g_rsxr_fret_halflen;
                f->verify = kVerifyFrames;
            }

            Unlock();
        }
    } else if (!want && applied) {
        const int set = GuardedRestore(guitar, grid);

        Lock();

        if (GuitarState* f = FindGuitar(guitar)) {
            f->applied = false;
            f->verify = 0;
        }

        Unlock();
        LogRestore(guitar, set);
    }

    if (recoverFloor) {
        RecoverFloor(guitar, grid);
    }

    if (verify && RsxrLog_Once(&g_state.logVerify)) {
        LogReadBack("read-back (7-string)", guitar, grid);
    }
}

void RsxrFrets_Forget(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    Lock();
    g_state.guitars.Erase(guitar);
    Unlock();
}

void RsxrFrets_Stats(RsxrFretsStats* out) {
    RsxrFretsStats st{};

    Lock();

    for (const auto& s : g_state.guitars) {
        if (!s.key) {
            continue;
        }

        ++st.guitars;
        if (s.value.applied) {
            ++st.applied;
        }
    }

    Unlock();
    *out = st;
}

namespace {
bool ChordFrameRaw(const uint8_t* guitar, int top, rsxrfrets::FrameFit* out) {
    uint8_t* g = const_cast<uint8_t*>(guitar);
    const float *a0, *a1;
    uint8_t* grid = UncheckedField<uint8_t*>(g, kGuitarGrid);
    if (!grid || !Attach(g, &a0, &a1, nullptr, nullptr)) {
        return false;
    }

    const float* b = reinterpret_cast<const float*>(Bar(grid, 0) + kBarBLocal);
    const int moved = rsxrfretglue::FloorMoved(guitar);
    const bool ok = rsxrfrets::ChordFrame(a0, a1, b, moved, top, out, StockFar(g), FarLanes(g));

    for (int i = 0; i < 3; ++i) {
        out->bLocal[i] = b[i];
        out->bWorld[i] = reinterpret_cast<const float*>(Bar(grid, 0) + kBarBWorld)[i];
        out->a0[i] = a0[i];
    }

    return ok;
}
}  // namespace

int rsxrfretglue::FloorMoved(const uint8_t* guitar) {
    Lock();
    const GuitarState* s = FindGuitar(guitar);
    const int moved = s && s->floorMoved > 0 ? s->floorMoved : 0;

    Unlock();

    return moved;
}

bool RsxrFrets_ChordFrame(const uint8_t* guitar, int top, rsxrfrets::FrameFit* out) {
    if (!guitar) {
        return false;
    }

    __try {
        return ChordFrameRaw(guitar, top, out);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

// Per song: the logs, and the glow bars FRG2 has not scaled yet (hooks_glow.cpp).
void RsxrFrets_ResetLog() {
    rsxrfretglue::ResetGlow();
    _InterlockedExchange(&g_state.logApply, 0);
    _InterlockedExchange(&g_state.logF1, 0);
    _InterlockedExchange(&g_state.logVerify, 0);
    _InterlockedExchange(&g_state.logFail, 0);
    _InterlockedExchange(&g_state.logFloor, 0);
    _InterlockedExchange(&g_state.logInlay, 0);
}
