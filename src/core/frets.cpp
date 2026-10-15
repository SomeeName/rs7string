#include "core/frets.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <intrin.h>

// No Win32 calls, no C++ exception frames on the path a stock song takes (no guitar wants 7
// strings): the emulator test runs F1 that way. Game functions are never called while the table
// lock is held. Logging sits in noinline helpers reached only on 7-string events or once per song.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_setscale = 0;
__declspec(dllexport) uintptr_t g_rs7_frets_on = 1;
__declspec(dllexport) float g_rs7_fret_halflen = rs7frets::kDefaultHalfLen;
}

namespace {
using namespace rs7frets;

template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

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
        pop ebx
    }
}

uint8_t* Bar(uint8_t* grid, int f) {
    return grid + kBars + uint32_t(f) * kBarStride;
}

void Midpoint(uint8_t* bar, float out[3]) {  // the stock translation of 0x58FB20 (0x58FBE0..0x58FC3F)
    const float* a = reinterpret_cast<const float*>(bar + kBarAWorld);
    const float* b = reinterpret_cast<const float*>(bar + kBarBWorld);

    for (int i = 0; i < 3; ++i) {
        out[i] = (a[i] + b[i]) * 0.5f;
    }
}

int BarCount(uint8_t* guitar) {
    uint8_t* info = At<uint8_t*>(guitar, kGuitarInfo);
    if (!info) {
        return 0;
    }

    const int n = info[0xD];

    return n > kMaxBars ? kMaxBars : n;
}

// String 0/1 attach points (local, GuitarStrings stringObj[0/1] +8) and world positions (+14h).
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

// ---- log lines ----
__declspec(noinline) void LogApply(const char* by, const uint8_t* guitar, const uint8_t* grid, int bars, int set,
                                   const Extend& x, const float* mid0, const float* new0) {
    try {
        Rs7Log("frets: %s applied (guitar %p, FretGrid %p): %d of %d bars re-placed and scaled; string spacing "
               "d=%.4f, shift=(%.4f, %.4f, %.4f) = d/2 towards string 6, scale k=%.4f (L=%.3f); bar 0 midpoint "
               "(%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)",
               by, guitar, grid, set, bars, double(x.spacing), double(x.shift[0]), double(x.shift[1]),
               double(x.shift[2]), double(x.scale), double(g_rs7_fret_halflen), double(mid0[0]), double(mid0[1]),
               double(mid0[2]), double(new0[0]), double(new0[1]), double(new0[2]));
    } catch (...) {
    }
}

__declspec(noinline) void LogRestore(const uint8_t* guitar, int set) {
    try {
        Rs7Log("frets: restored (guitar %p): %d bars back on the stock midpoint, scale 1 (fret_extend switched off "
               "(overlay))",
               guitar, set);
    } catch (...) {
    }
}

__declspec(noinline) void LogFail(const char* why, const uint8_t* guitar) {
    try {
        Rs7Log("frets: NOT applied (guitar %p): %s", guitar, why);
    } catch (...) {
    }
}

// One bar's root node and its first NiMesh: local / world transform and bounds, and the mesh
// bound's span along the string axis u next to strings 5, 0 and 6 (the bound sphere is an upper
// bound of the real wire length).
__declspec(noinline) void LogBar(const char* tag, int f, uint8_t* entity, const float* u, const float* w0,
                                 const float* w1) {
    uint8_t* root = rs7s7d::EntityRoot(entity);
    float lt[3] = {}, ls = 0, wt[3] = {}, ws = 0, rb[4] = {};

    if (root) {
        rs7s7d::SafeCopy(lt, root + 0x70, 12);
        rs7s7d::SafeCopy(&ls, root + 0x7C, 4);
        rs7s7d::SafeCopy(wt, root + 0xA4, 12);
        rs7s7d::SafeCopy(&ws, root + 0xB0, 4);
        rs7s7d::SafeCopy(rb, root + 0x2C, 16);
    }

    // Depth-first search for the first NiMesh (fretShape).
    uint8_t* stack[64];
    int sp = 0, visited = 0;
    uint8_t* mesh = nullptr;

    if (root) {
        stack[sp++] = root;
    }

    while (sp && !mesh && visited < 64) {
        uint8_t* n = stack[--sp];

        ++visited;

        if (rs7s7d::IsKindOf(n, "NiMesh")) {
            mesh = n;
            break;
        }

        if (!rs7s7d::IsKindOf(n, "NiNode")) {
            continue;
        }

        uint8_t* arr = nullptr;
        uint16_t slots = 0;

        rs7s7d::SafeCopy(&arr, n + 0xD8, 4);
        rs7s7d::SafeCopy(&slots, n + 0xDC, 2);

        for (int i = int(slots) - 1; arr && i >= 0 && sp < 64; --i) {
            uint8_t* c = nullptr;

            rs7s7d::SafeCopy(&c, arr + i * 4, 4);
            if (c) {
                stack[sp++] = c;
            }
        }
    }

    float mb[4] = {}, mw[4] = {}, ms = 0;
    char name[48] = {};

    if (mesh) {
        rs7s7d::SafeCopy(mw, mesh + 0x2C, 16);
        rs7s7d::SafeCopy(mb, mesh + 0xF0, 16);
        rs7s7d::SafeCopy(&ms, mesh + 0xB0, 4);
        rs7s7d::NameOfNode(mesh, name, sizeof name);
    }

    auto dot = [&](const float* p) {
        return p[0] * u[0] + p[1] * u[1] + p[2] * u[2];
    };
    float s5[3], s6[3];

    for (int i = 0; i < 3; ++i) {
        s5[i] = w0[i] - 5.0f * (w0[i] - w1[i]);
        s6[i] = 2.0f * w0[i] - w1[i];
    }

    const float c = dot(mw);

    try {
        Rs7Log("frets %s: bar %d entity %p root %p: local t=(%.3f, %.3f, %.3f) s=%.4f, world t=(%.3f, %.3f, %.3f) "
               "s=%.4f, root bound c=(%.3f, %.3f, %.3f) r=%.3f; mesh %p \"%s\" world bound c=(%.3f, %.3f, %.3f) "
               "r=%.3f world s=%.4f, model bound c=(%.3f, %.3f, %.3f) r=%.3f; along the strings: mesh bound "
               "%.3f .. %.3f, string 5 %.3f, string 0 %.3f, string 6 %.3f",
               tag, f, entity, root, double(lt[0]), double(lt[1]), double(lt[2]), double(ls), double(wt[0]),
               double(wt[1]), double(wt[2]), double(ws), double(rb[0]), double(rb[1]), double(rb[2]), double(rb[3]),
               mesh, name, double(mw[0]), double(mw[1]), double(mw[2]), double(mw[3]), double(ms), double(mb[0]),
               double(mb[1]), double(mb[2]), double(mb[3]), double(c - mw[3]), double(c + mw[3]), double(dot(s5)),
               double(dot(w0)), double(dot(s6)));
    } catch (...) {
    }
}

__declspec(noinline) void LogReadBack(const char* tag, uint8_t* guitar, uint8_t* grid) {
    const float *a0, *a1, *w0, *w1;
    if (!grid || !Attach(guitar, &a0, &a1, &w0, &w1)) {
        return;
    }

    float d[3], len = 0;

    for (int i = 0; i < 3; ++i) {
        d[i] = a0[i] - a1[i];
        len += d[i] * d[i];
    }

    len = std::sqrt(len);
    if (!(len > 1e-3f)) {
        return;
    }

    for (float& v : d) {
        v /= len;
    }

    const int bars = BarCount(guitar);
    const int pick[3] = {0, 1, 12};

    for (int f : pick) {
        if (f < bars) {
            if (uint8_t* e = At<uint8_t*>(Bar(grid, f), kBarEntity)) {
                LogBar(tag, f, e, d, w0, w1);
            }
        }
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
}  // namespace

bool rs7frets::Compute(const float a0[3], const float a1[3], const float mid[3], float halfLen, Extend* out) {
    float d[3], len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        d[i] = a0[i] - a1[i];
        len2 += d[i] * d[i];
    }

    const float len = std::sqrt(len2);
    if (!(len > 1e-3f) || !(len < 1e3f) || !(halfLen > 0.0f)) {
        return false;
    }

    // Along u (unit, string 1 -> string 0 -> string 6), relative to string 0: the stock bar spans c -+ L.
    float c = 0.0f;

    for (int i = 0; i < 3; ++i) {
        c += (mid[i] - a0[i]) * d[i] / len;
    }

    const float m0 = c + halfLen;                         // margin beyond string 0
    const float m5 = (c - halfLen) * -1.0f - 5.0f * len;  // margin beyond string 5 (s5 at -5 len)
    const float margin = m0 > m5 ? m0 : m5;               // the wider stock margin, now beyond string 6
    const float top = len + margin, bottom = c - halfLen;
    const float along = 0.5f * (top + bottom) - c;  // centre shift along u

    out->scale = 0.5f * (top - bottom) / halfLen;

    for (int i = 0; i < 3; ++i) {
        out->shift[i] = along * d[i] / len;
    }

    out->spacing = len;

    return out->scale > 0.0f && std::isfinite(out->scale);
}

bool rs7frets::FloorShift(const float w0[3], const float w1[3], const float b[3], float out[3]) {
    float u[3], len2 = 0.0f, side = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = w0[i] - w1[i];
        len2 += u[i] * u[i];
        side += (b[i] - w0[i]) * u[i];
    }

    if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f)) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        out[i] = u[i];
    }

    return true;
}

bool rs7frets::ParseHalfLen(const wchar_t* text, float* out) {
    if (!text) {
        return false;
    }

    while (std::iswspace(*text)) {
        ++text;
    }

    if (!*text) {
        return false;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text) {
        return false;
    }

    while (std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < 1.0 || v > 20.0) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}

bool rs7frets::ChordFrame(const float a0[3], const float a1[3], const float b[3], bool floorMoved, int top,
                          FrameFit* out) {
    float u[3], len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = a0[i] - a1[i];
        len2 += u[i] * u[i];
    }

    const float d = std::sqrt(len2);
    if (!(d > 1e-3f) || !(d < 1e3f)) {
        return false;
    }

    // Coordinate p along u (string 1 -> string 0 -> string 6), 0 at string 0: string i lies at p = -i d.
    float pb = 0.0f;

    for (int i = 0; i < 3; ++i) {
        pb += (b[i] - a0[i]) * u[i] / d;
    }

    const bool six = pb > 0.0f;                 // the origin edge lies beyond string 0 / 6's side
    const float m = six ? pb : -pb - 5.0f * d;  // stock margin of the frame beyond lanes 0 / 5
    if (!(m > 0.05f * d) || !(m < 4.0f * d) || !std::isfinite(m)) {
        return false;
    }

    const float h = 5.0f * d + 2.0f * m;                         // the model's extent
    const float po = pb + (six && floorMoved ? d : 0.0f);        // origin edge as placed now
    const float lo = -5.0f * d - m;                              // string 5 end, margin m
    const float hi = (top > 5 ? float(top - 5) * d : 0.0f) + m;  // string 6 / `top` (or string 0) end, margin m
    // The origin edge stays where the game puts it (on the floor); only the far edge moves, so the frame's height
    // grows. The origin edge moves out only when it would not reach the lane on its own side (the floor of a guitar
    // whose fret bars were not extended, or the 8th lane).
    float origin, farEdge;

    if (six) {
        origin = po + (hi > po ? hi - po : 0.0f);  // beyond string 6's side: p grows
        farEdge = lo;
    } else {
        origin = po - (po > lo ? po - lo : 0.0f);
        farEdge = hi;
    }

    out->scale = std::fabs(farEdge - origin) / h;
    out->shift = six ? po - origin : origin - po;  // along the extent, from the origin edge towards the lanes
    out->origin = po;
    out->margin = m;
    out->spacing = d;

    return std::isfinite(out->scale) && std::isfinite(out->shift) && out->scale > 0.0f;
}

namespace {
volatile long g_logFloor = 0, g_logInlay = 0;

__declspec(noinline) void LogInlay(const uint8_t* guitar, const float* p, float dz) {
    try {
        Rs7Log("frets: IN1 inlays of guitar %p centred on 7 strings: entity at (%.3f, %.3f, %.3f) (string axis %+.4f)",
               guitar, double(p[0]), double(p[1]), double(p[2]), double(dz));
    } catch (...) {
    }
}

__declspec(noinline) void LogFloor(const uint8_t* guitar, const float* before, const float* shift, int n) {
    try {
        Rs7Log(
            "frets: highway floor of guitar %p (the fret-wire bottom ends lie on string 6's side: inverted strings): "
            "%d ends moved by (%.4f, %.4f, %.4f); bar 0 end (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)",
            guitar, n, double(shift[0]), double(shift[1]), double(shift[2]), double(before[0]), double(before[1]),
            double(before[2]), double(before[0] + shift[0]), double(before[1] + shift[1]),
            double(before[2] + shift[2]));
    } catch (...) {
    }
}

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
