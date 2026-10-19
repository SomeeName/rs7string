// The fret bars' game glue (frets.h): F1 / F2 re-place and scale the bars of a guitar built with 7/8 strings, the
// highway floor, the IN1 inlays and the chord-frame geometry B2 reads. The logic it calls is frets.cpp, its log
// lines are hooks_log.cpp.
#include "features/frets/hooks.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>
#include <intrin.h>

// No Win32 calls, no C++ exception frames on the path a stock song takes (no guitar wants 7
// strings): the emulator test runs F1 that way. Game functions are never called while the table
// lock is held. Logging sits in noinline helpers reached only on 7-string events or once per song.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_setscale = 0;
__declspec(dllexport) uintptr_t g_rs7_frets_on = 1;
__declspec(dllexport) float g_rs7_fret_halflen = rs7frets::kDefaultHalfLen;
}

namespace rs7fretglue {
using namespace rs7frets;

uint8_t* Bar(uint8_t* grid, int f) {
    return grid + kBars + uint32_t(f) * kBarStride;
}

int BarCount(uint8_t* guitar) {
    uint8_t* info = At<uint8_t*>(guitar, kGuitarInfo);
    if (!info) {
        return 0;
    }

    const int n = info[0xD];

    return n > kMaxBars ? kMaxBars : n;
}

bool Attach(uint8_t* guitar, const float** a0, const float** a1, const float** w0, const float** w1) {
    uint8_t* gs = At<uint8_t*>(guitar, kGuitarStrings);
    uint8_t* o0 = gs ? At<uint8_t*>(gs, 4) : nullptr;
    uint8_t* o1 = gs ? At<uint8_t*>(gs, 8) : nullptr;
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
}  // namespace rs7fretglue

namespace {
using namespace rs7frets;
using namespace rs7fretglue;

struct State {
    uint8_t* guitar;  // Rnd_Guitar* (key; nullptr = free slot)
    uint8_t* grid;    // its FretGrid at the last N5 frame
    bool applied;     // bars re-placed and scaled (by F1 or F2) and not restored since
    float halfLen;    // the fret_halflen of that apply (overlay: a change re-applies)
    int verify;       // N5 frames until the node read-back (0 = none pending)
};

State g_st[kMaxGuitars];
volatile long g_spin = 0;
volatile long g_logApply = 0, g_logF1 = 0, g_logRestore = 0, g_logVerify = 0, g_logFail = 0;

void Lock() {
    while (_InterlockedCompareExchange(&g_spin, 1, 0) != 0) {
        _mm_pause();
    }
}

void Unlock() {
    _InterlockedExchange(&g_spin, 0);
}

bool Once(volatile long* flag) {
    return _InterlockedExchange(flag, 1) == 0;
}

State* FindGuitar(const void* guitar) {  // caller holds the lock
    for (State& s : g_st) {
        if (s.guitar == guitar) {
            return &s;
        }
    }

    return nullptr;
}

State* FindOrAdd(uint8_t* guitar, uint8_t* grid) {  // caller holds the lock
    State* s = FindGuitar(guitar);
    if (s) {
        return s;
    }

    for (State& x : g_st) {
        if (!s && !x.guitar) {
            s = &x;
        }
    }

    if (!s) {
        s = &g_st[0];
    }
    *s = State{guitar, grid, false, 0.0f, 0};

    return s;
}

// C-lite: the bars are laid out for the string count the guitar was BUILT with (InstrumentInfo +0Bh):
// 7 or 8 strings = extended, 4 / 6 = stock. The count never changes during the guitar's life.
bool WantsSeven(const uint8_t* guitar) {
    if (!guitar) {
        return false;
    }

    const uint8_t* info = *reinterpret_cast<uint8_t* const*>(guitar + kGuitarInfo);

    return info && (info[0x0B] == 7 || info[0x0B] == 8);
}

// ---- game calls ----
void GameSetPos(uint8_t* entity, const float* v) {  // Entity_SetTranslation 0x7ADF70 cdecl(entity, &vec3)
    using Fn = void(__cdecl*)(uint8_t*, const float*);

    reinterpret_cast<Fn>(g_rs7_fn_setpos)(entity, v);
}

void GameSetScale(uint8_t* entity, float k) {  // Entity_SetScale 0x7AE2C0: EAX = entity, cdecl(float)
    __asm {
        push ebx
        push esi
        push edi
        push k
        mov eax, entity
        call dword ptr [g_rs7_fn_setscale]
        add esp, 4
        pop edi
        pop esi
        pop ebx                    // ebx / esi / edi are kept across the game's setter
    }
}

void Midpoint(uint8_t* bar, float out[3]) {  // the stock translation of 0x58FB20 (0x58FBE0..0x58FC3F)
    const float* a = reinterpret_cast<const float*>(bar + kBarAWorld);
    const float* b = reinterpret_cast<const float*>(bar + kBarBWorld);

    for (int i = 0; i < 3; ++i) {
        out[i] = (a[i] + b[i]) * 0.5f;
    }
}

// Re-place and scale every bar of `guitar` (F1 right after the stock body, F2 on a change). Returns
// the number of bars set; 0 = nothing to do yet (no bars / strings loaded).
int Apply(uint8_t* guitar, uint8_t* grid, const char* by) {
    if (!g_rs7_fn_setpos || !g_rs7_fn_setscale || !grid) {
        return 0;
    }

    const float *a0, *a1;
    Extend x;

    if (!Attach(guitar, &a0, &a1, nullptr, nullptr)) {
        if (Once(&g_logFail)) {
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

    if (!Compute(a0, a1, m0, g_rs7_fret_halflen, &x)) {
        if (Once(&g_logFail)) {
            LogFail("degenerate string spacing", guitar);
        }
        return 0;
    }

    const int bars = BarCount(guitar);
    int set = 0;
    float mid0[3] = {}, new0[3] = {};

    for (int f = 0; f < bars; ++f) {
        uint8_t* bar = Bar(grid, f);
        uint8_t* entity = At<uint8_t*>(bar, kBarEntity);
        if (!entity) {
            continue;
        }

        float p[3];

        Midpoint(bar, p);
        if (f == 0) {
            std::memcpy(mid0, p, sizeof p);
        }

        for (int i = 0; i < 3; ++i) {
            p[i] += x.shift[i];
        }

        if (f == 0) {
            std::memcpy(new0, p, sizeof p);
        }
        GameSetPos(entity, p);
        GameSetScale(entity, x.scale);
        ++set;
    }

    if (set && Once(by[0] == 'F' && by[1] == '1' ? &g_logF1 : &g_logApply)) {
        LogApply(by, guitar, grid, bars, set, x, mid0, new0);
    }

    return set;
}

int Restore(uint8_t* guitar, uint8_t* grid) {
    if (!g_rs7_fn_setpos || !g_rs7_fn_setscale || !grid) {
        return 0;
    }

    const int bars = BarCount(guitar);
    int set = 0;

    for (int f = 0; f < bars; ++f) {
        uint8_t* bar = Bar(grid, f);
        uint8_t* entity = At<uint8_t*>(bar, kBarEntity);
        if (!entity) {
            continue;
        }

        float p[3];

        Midpoint(bar, p);
        GameSetPos(entity, p);
        GameSetScale(entity, 1.0f);
        ++set;
    }

    return set;
}

volatile long g_logFloor = 0, g_logInlay = 0;

// The floor (frets.h): 0x58FB20 has just written every bar's world ends from the locators (+the guitar offset);
// they stay as set here until its next run, so every reader this frame sees one floor.
void ShiftFloor(uint8_t* guitar, uint8_t* grid) {
    const float *a0, *a1;
    if (!Attach(guitar, &a0, &a1, nullptr, nullptr)) {
        return;
    }

    uint8_t* bar0 = Bar(grid, 0);
    float shift[3];
    if (!*bar0 || !FloorShift(a0, a1, reinterpret_cast<const float*>(bar0 + kBarBLocal), shift)) {
        return;
    }

    int n = BarCount(guitar) + 1;  // the time axis / fret numbers read records 0..count

    if (n > kMaxBars) {
        n = kMaxBars;
    }
    float first[3];

    std::memcpy(first, bar0 + kBarBWorld, sizeof first);

    for (int f = 0; f < n; ++f) {
        float* b = reinterpret_cast<float*>(Bar(grid, f) + kBarBWorld);

        for (int i = 0; i < 3; ++i) {
            b[i] += shift[i];
        }
    }

    if (Once(&g_logFloor)) {
        LogFloor(guitar, first, shift, n);
    }
}
}  // namespace

extern "C" void __cdecl Rs7_InlaysAfter(uint8_t* guitar, uint8_t* inlays) {
    if (!g_rs7_frets_on || !inlays || !WantsSeven(guitar) || !g_rs7_fn_setpos) {
        return;
    }

    uint8_t* entity = At<uint8_t*>(inlays, 4);
    uint8_t* head = At<uint8_t*>(guitar, 0x18);
    uint8_t* frame = head ? At<uint8_t*>(head, 4) : nullptr;
    uint8_t* froot = frame ? rs7s7d::EntityRoot(frame) : nullptr;
    const float *a0, *a1;
    if (!entity || !froot || !Attach(guitar, &a0, &a1, nullptr, nullptr)) {
        return;
    }

    float p[3];
    // The frame's placement, which 0x59BA80 gives the inlay entity (its root's world translation: (1,1,1) in a song,
    // (0,0,-1000) in the tuner, the frame root has no parent transform).
    if (!rs7s7d::SafeCopy(p, froot + 0xA4, sizeof p)) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        p[i] += 0.5f * (a0[i] - a1[i]);
    }

    GameSetPos(entity, p);
    if (Once(&g_logInlay)) {
        LogInlay(guitar, p, 0.5f * (a0[2] - a1[2]));
    }
}

extern "C" void __cdecl Rs7_FretsAfterApply(uint8_t* grid) {
    if (!g_rs7_frets_on || !grid) {
        return;
    }

    uint8_t* guitar = At<uint8_t*>(grid, 0);  // 0x58FB2B: [grid] = the Rnd_Guitar (+24h info)
    if (!WantsSeven(guitar)) {
        return;  // every guitar built with 6 strings / bass
    }

    const int set = Apply(guitar, grid, "F1 (after Rnd_FretBars_ApplyTransforms 0x58FB20)");

    ShiftFloor(guitar, grid);
    if (!set) {
        return;
    }

    Lock();
    State* s = FindOrAdd(guitar, grid);

    s->applied = true;
    s->halfLen = g_rs7_fret_halflen;
    s->grid = grid;
    if (!s->verify) {
        s->verify = kVerifyFrames;
    }
    Unlock();
}

void Rs7Frets_Frame(uint8_t* guitar, bool want) {
    if (!guitar) {
        return;
    }

    // fret_extend is live (overlay): switched off, an applied guitar is restored below and nothing
    // else is touched; a guitar without a State returns here as before.
    const bool on = g_rs7_frets_on != 0;

    want = want && on;
    uint8_t* grid = At<uint8_t*>(guitar, kGuitarGrid);

    Lock();
    State* s = FindGuitar(guitar);

    if (!on && !(s && s->applied)) {
        Unlock();
        return;
    }

    if (!s && want) {
        s = FindOrAdd(guitar, grid);
    }

    if (!s) {
        Unlock();
        return;  // a guitar built with 6 strings / bass: nothing to do
    }

    // fret_halflen changed while applied (overlay): apply again (Apply starts from the stock midpoints).
    const bool reapply = s->applied && want && s->halfLen != g_rs7_fret_halflen;
    const bool applied = s->applied && !reapply;

    s->grid = grid;
    int verify = 0;

    if (s->verify > 0) {
        verify = --s->verify == 0 ? 1 : 0;
    }
    Unlock();

    if (want && !applied) {
        // F1 did not (or could not yet: no attach points) place the bars after the guitar's last ApplyTransforms.
        const int set =
            Apply(guitar, grid, reapply ? "F2 (fret_halflen changed)" : "F2 (N5: guitar built with 7 strings)");

        if (reapply) {
            Rs7Log("frets: %d bars re-applied with fret_halflen=%.3f (overlay)", set, double(g_rs7_fret_halflen));
        }

        if (set) {
            Lock();

            if (State* f = FindGuitar(guitar)) {
                f->applied = true;
                f->halfLen = g_rs7_fret_halflen;
                f->verify = kVerifyFrames;
            }

            Unlock();
        }
    } else if (!want && applied) {
        const int set = Restore(guitar, grid);

        Lock();

        if (State* f = FindGuitar(guitar)) {
            f->applied = false;
            f->verify = 0;
        }

        Unlock();
        LogRestore(guitar, set);
    }

    if (verify && Once(&g_logVerify)) {
        LogReadBack("read-back (7-string)", guitar, grid);
    }
}

void Rs7Frets_Forget(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    Lock();
    if (State* s = FindGuitar(guitar)) {
        *s = State{};
    }
    Unlock();
}

void Rs7Frets_ResetLog() {
    _InterlockedExchange(&g_logApply, 0);
    _InterlockedExchange(&g_logF1, 0);
    _InterlockedExchange(&g_logRestore, 0);
    _InterlockedExchange(&g_logVerify, 0);
    _InterlockedExchange(&g_logFail, 0);
    _InterlockedExchange(&g_logFloor, 0);
    _InterlockedExchange(&g_logInlay, 0);
}

void Rs7Frets_Stats(Rs7FretsStats* out) {
    Rs7FretsStats st{};

    Lock();

    for (const State& s : g_st) {
        if (!s.guitar) {
            continue;
        }

        ++st.guitars;
        if (s.applied) {
            ++st.applied;
        }
    }

    Unlock();
    *out = st;
}

namespace {
bool ChordFrameRaw(const uint8_t* guitar, int top, rs7frets::FrameFit* out) {
    uint8_t* g = const_cast<uint8_t*>(guitar);
    const float *a0, *a1;
    uint8_t* grid = At<uint8_t*>(g, kGuitarGrid);
    if (!grid || !Attach(g, &a0, &a1, nullptr, nullptr)) {
        return false;
    }

    const float* b = reinterpret_cast<const float*>(Bar(grid, 0) + kBarBLocal);
    float shift[3];
    const bool moved = g_rs7_frets_on && WantsSeven(g) && *Bar(grid, 0) && FloorShift(a0, a1, b, shift);
    const bool ok = rs7frets::ChordFrame(a0, a1, b, moved, top, out);

    for (int i = 0; i < 3; ++i) {
        out->bLocal[i] = b[i];
        out->bWorld[i] = reinterpret_cast<const float*>(Bar(grid, 0) + kBarBWorld)[i];
        out->a0[i] = a0[i];
    }

    return ok;
}
}  // namespace

bool Rs7Frets_ChordFrame(const uint8_t* guitar, int top, rs7frets::FrameFit* out) {
    if (!guitar) {
        return false;
    }

    __try {
        return ChordFrameRaw(guitar, top, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
