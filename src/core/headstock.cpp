#include "core/headstock.h"
#include "core/colournative.h"
#include "core/frets.h"
#include "core/pegcolour.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "core/tuner.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <intrin.h>

// Game thread only (the PegSet ctor / load of Rnd_Guitar_LoadStep, N5, the tuner). The pure helpers
// (IsNative, InitTail, LoadedCounts, NamedColour) make no game call, so the unit tests and the
// emulator run them. No Win32 call and no C++ exception frame on the PSC1 / PSL2 paths.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_head_on = 1;
__declspec(dllexport) uintptr_t g_rs7_native_pegset = 0;
__declspec(dllexport) uintptr_t g_rs7_peg_load = 7;
__declspec(dllexport) uintptr_t g_rs7_fn_euler = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_pegplace = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_peglayer = 0;
__declspec(dllexport) uintptr_t g_rs7_pegnames = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_pegselect = 0;
__declspec(dllexport) uintptr_t g_rs7_pegsets[RS7_PEG_OWNERS] = {};
}

extern "C" __declspec(dllexport) void __cdecl Rs7_HeadLog(const char* fmt, const void* a, const void* b, const void* c);

namespace {
using namespace rs7head;

constexpr uint32_t kFlags = 0x20;  // NiAVObject u16 flags, bit 0 APP_CULLED
constexpr uint32_t kWorldRot = 0x80;
constexpr uint32_t kWorldPos = 0xA4;
constexpr uint32_t kChildren = 0xD8;
constexpr uint32_t kChildSlots = 0xDC;
constexpr float kRadToDegNeg = -57.2957763671875f;  // 0x12246D8 (double), used by 0x59C67D..0x59C6A5
constexpr int kMaxNodes = 128;

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

volatile long g_ctors = 0, g_loads7 = 0, g_active7 = 0, g_foreign = 0, g_ownFull = 0;
volatile long g_logOwnFull = 0;
volatile long g_logCtor = 0, g_logLoad = 0, g_logRaise = 0, g_logNoSlot = 0, g_logForeign = 0;

bool Once(volatile long* f) {
    return _InterlockedExchange(f, 1) == 0;
}

bool Upto(volatile long* f, long n) {
    return _InterlockedIncrement(f) <= n;
}

uint8_t* PegSetOf(uint8_t* guitar) {
    uint8_t* head = guitar ? At<uint8_t*>(guitar, kGuitarHead) : nullptr;

    return head ? At<uint8_t*>(head, kHeadPegs) : nullptr;
}

// ---- game calls ----
void GameShowEntity(uint8_t* entity, uint32_t show) {  // 0x7AE370: EAX = entity, cdecl(show)
    if (!g_rs7_fn_show || !entity) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push show
        mov eax, entity
        call dword ptr [g_rs7_fn_show]
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
}

void GamePegSetCall(uintptr_t fn, uint8_t* pegset) {  // 0x59C630 / 0x59C6E0: stdcall(PegSet)
    if (!fn || !pegset) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push pegset
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 for one peg, as the PegSet fade loops
// 0x59C95C / 0x59C89C call them: EAX = entity, (ClipFade manager, seconds, 0, 1.0) ret 10h.
void GameFade(uintptr_t fn, uint8_t* entity, float seconds) {
    uint8_t* mgr = g_rs7_fade_mgr ? *reinterpret_cast<uint8_t**>(g_rs7_fade_mgr) : nullptr;
    if (!fn || !mgr || !entity) {
        return;
    }

    const float zero = 0.0f, one = 1.0f;

    __asm {
        push ebx
        push esi
        push edi
        push one
        push zero
        push seconds
        push mgr
        mov eax, entity
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

void GameEuler(const uint8_t* rot, float* e) {  // 0x8FCAA0 (radians, then scaled like 0x59C680..)
    float* e0 = e;
    float* e1 = e + 1;
    float* e2 = e + 2;

    __asm {
        push ebx
        push esi
        push edi
        mov esi, rot
        mov edi, e2
        push e1
        push e0
        call dword ptr [g_rs7_fn_euler]
        pop edi
        pop esi
        pop ebx
    }

    for (int i = 0; i < 3; ++i) e[i] *= kRadToDegNeg;
}

void GameSetVec(uintptr_t fn, uint8_t* entity, const float* v) {
    using Fn = void(__cdecl*)(uint8_t*, const float*);

    reinterpret_cast<Fn>(fn)(entity, v);
}

void GameSetScaleU(uint8_t* entity, float k) {  // Entity_SetScale 0x7AE2C0: EAX = entity, cdecl(float)
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

// ---- the active count: raise to 7 / lower to 6 ----
uint8_t& Loaded(uint8_t* ps) {
    return At<uint8_t>(ps, kTail + 1);
}

// Loads PSL2 completed, for N5 to log (PSL2 runs inside the guitar's resource-loader job: no logging there).
LoadRing g_loads;

bool Raise(uint8_t* ps, const char* by) {
    const unsigned count = At<uint16_t>(ps, kPegCount);
    if (count >= 7) {
        return true;
    }

    uint8_t* slot = Slot(ps, kStockSlots);
    uint8_t* peg = At<uint8_t*>(slot, kSlotEntity);

    if (count != 6 || Loaded(ps) < 7 || !At<uint8_t>(ps, kPegLoaded) || !slot[0] || !peg ||
        !At<uint8_t*>(slot, kSlotLocator)) {
        if (Loaded(ps) >= 7 && Once(&g_logNoSlot)) {
            Rs7_HeadLog("headstock: PegSet %p: no 7th peg to show (slot 6 %s; the model has no Locator_Peg_7?)", ps,
                        slot[0] ? "taken" : "NOT taken: the stock binder 0x59C980 found no locator", nullptr);
        }
        return false;
    }

    At<uint16_t>(ps, kPegCount) = 7;
    GameShowEntity(peg, 1);
    GamePegSetCall(g_rs7_fn_pegplace, ps);  // Rnd_PegSet_ApplyTransforms over the seven
    GamePegSetCall(g_rs7_fn_peglayer, ps);  // the render layer, as 0x58DD20 does
    // Peg 7 joins the PegSet's current fade state, with the call the stock fade loops make per peg.
    const bool out = At<uint8_t>(ps, kPegFadedOut) != 0, in = At<uint8_t>(ps, kPegFadedIn) != 0;

    if (out) {
        GameFade(g_rs7_fn_fadeout, peg, 0.01f);
    } else if (in) {
        GameFade(g_rs7_fn_fadein, peg, kRaiseFadeIn);
    }
    _InterlockedIncrement(&g_active7);

    if (Upto(&g_logRaise, 6)) {
        Rs7_HeadLog(in || out ? "headstock: PegSet %p: 7 pegs active (%s), peg 7 %p joins the PegSet's fade state"
                              : "headstock: PegSet %p: 7 pegs active (%s), peg 7 %p (no fade requested yet)",
                    ps, by, peg);
    }

    return true;
}

// ---- debug experiments: [debug] head_cull / head_stretch (hot-reloaded) ----
wchar_t g_ini[MAX_PATH] = {};
char g_cullName[48] = {};
float g_stretch = 1.0f, g_stretchApplied = 1.0f;
int g_stretchMode = 0;
uint8_t* g_culled = nullptr;
int g_reloadFrames = 0;
volatile long g_logStretch = 0;

struct Live {
    char cull[48];
    float st;
    int mode;
};

Live g_live = {};
volatile long g_liveSpin = 0, g_liveSeq = 0, g_liveOwned = 0;
long g_liveTaken = 0;

bool Take(const char* cull, float st, int mode) {
    bool changed = false;
    char narrow[48] = {};

    for (int i = 0; i < 47 && cull[i]; ++i) {
        narrow[i] = char(cull[i] & 0x7F);
    }

    if (std::strcmp(narrow, g_cullName) != 0) {
        std::memcpy(g_cullName, narrow, sizeof narrow);
        changed = true;
    }

    changed = changed || mode != g_stretchMode || st != g_stretch;
    g_stretchMode = mode;
    g_stretch = st;

    return changed;
}

float ReadFloat(const wchar_t* key, float def, float lo, float hi) {
    wchar_t buf[64] = {};

    GetPrivateProfileStringW(L"debug", key, L"", buf, 64, g_ini);
    wchar_t* end = nullptr;
    const double v = std::wcstod(buf, &end);
    if (!buf[0] || end == buf || !std::isfinite(v) || v < lo || v > hi) {
        return def;
    }

    return float(v);
}

void Reload() {
    if (g_liveOwned) {
        const long seq = g_liveSeq;
        if (seq == g_liveTaken) {
            return;
        }

        while (_InterlockedCompareExchange(&g_liveSpin, 1, 0) != 0) {
            _mm_pause();
        }

        const Live l = g_live;

        _InterlockedExchange(&g_liveSpin, 0);
        g_liveTaken = seq;
        Take(l.cull, l.st, l.mode);
        return;
    }

    if (!g_ini[0] || ++g_reloadFrames < 120) {
        return;
    }

    g_reloadFrames = 0;
    wchar_t cull[48] = {};

    GetPrivateProfileStringW(L"debug", L"head_cull", L"", cull, 48, g_ini);
    char narrow[48] = {};

    for (int i = 0; i < 47 && cull[i]; ++i) {
        narrow[i] = char(cull[i] & 0x7F);
    }

    Take(narrow, ReadFloat(L"head_stretch", 1.0f, 0.5f, 2.0f), int(ReadFloat(L"head_stretch_mode", 0.0f, 0.0f, 1.0f)));
}

int Collect(uint8_t* root, uint8_t** out) {  // depth first, at most kMaxNodes nodes
    int n = 0, sp = 0;
    uint8_t* stack[kMaxNodes];

    stack[sp++] = root;

    while (sp && n < kMaxNodes) {
        uint8_t* p = stack[--sp];

        out[n++] = p;
        if (!rs7s7d::IsKindOf(p, "NiNode")) {
            continue;
        }

        const unsigned cnt = Rd<uint16_t>(p, kChildSlots);
        const uint8_t* ch = Rd<const uint8_t*>(p, kChildren);

        for (int i = int(cnt < 64 ? cnt : 64) - 1; i >= 0; --i) {
            uint8_t* c = Rd<uint8_t*>(ch, 4 * uint32_t(i));

            if (c && sp < kMaxNodes) {
                stack[sp++] = c;
            }
        }
    }

    return n;
}

// Diagnostic: keep the named frame node culled (un-cull the previous one when the name changes).
void ApplyCull(uint8_t* frame) {
    uint8_t* target = nullptr;
    uint8_t* fr = (g_cullName[0] && frame) ? rs7s7d::EntityRoot(frame) : nullptr;

    if (fr) {
        uint8_t* nodes[kMaxNodes];
        const int n = Collect(fr, nodes);

        for (int i = 0; i < n && !target; ++i) {
            char name[48] = {};

            rs7s7d::NameOfNode(nodes[i], name, sizeof name);
            if (!std::strcmp(name, g_cullName)) {
                target = nodes[i];
            }
        }
    }

    if (g_culled && g_culled != target) {
        const uint16_t f = uint16_t(Rd<uint16_t>(g_culled, kFlags) & ~1u);

        rs7s7d::SafeStore(g_culled + kFlags, &f, sizeof f);
    }

    g_culled = target;

    if (target) {
        const uint16_t f = uint16_t(Rd<uint16_t>(target, kFlags) | 1u);

        rs7s7d::SafeStore(target + kFlags, &f, sizeof f);
    }
}

// Experiment: stretch the frame along y about the nut, re-place the pegs on their (stretched)
// locators. k = 1 restores the stock rotate / translation.
void ApplyStretch(uint8_t* guitar, uint8_t* head, uint8_t* pegset) {
    uint8_t* frame = At<uint8_t*>(head, kHeadFrame);
    uint8_t* root = frame ? rs7s7d::EntityRoot(frame) : nullptr;
    uint8_t* gs = At<uint8_t*>(guitar, 0x0C);
    uint8_t* obj0 = gs ? At<uint8_t*>(gs, 4) : nullptr;
    if (!root || !obj0 || !g_rs7_fn_setpos || !g_rs7_fn_setrot || !g_rs7_fn_euler) {
        return;
    }

    const float k = g_stretch;
    const float* o = reinterpret_cast<const float*>(guitar + 0x2C);
    const float nutY = At<float>(obj0, 0x14 + 4);
    const float m[9] = {1.0f, 0.0f, 0.0f, 0.0f, k, 0.0f, 0.0f, 0.0f, 1.0f};

    rs7s7d::SafeStore(root + 0x4C, m, sizeof m);
    const float t[3] = {o[0], o[1] + (nutY - o[1]) * (1.0f - k), o[2]};

    GameSetVec(g_rs7_fn_setpos, frame, t);
    if (g_stretchMode == 1 && g_rs7_fn_setscale) {
        GameSetScaleU(frame, k);
    }
    if (Once(&g_logStretch)) {
        Rs7_HeadLog("headstock stretch (experiment): frame root %p, k %p/1000, nut y %p/1000", root,
                    reinterpret_cast<void*>(intptr_t(k * 1000.0f)), reinterpret_cast<void*>(intptr_t(nutY * 1000.0f)));
    }
    g_stretchApplied = k;
    const unsigned count = Rd<uint16_t>(pegset, kPegCount);

    for (unsigned s = 0; s < count && s < unsigned(kCapacity); ++s) {
        uint8_t* slot = Slot(pegset, int(s));
        uint8_t* peg = At<uint8_t*>(slot, kSlotEntity);
        uint8_t* loc = At<uint8_t*>(slot, kSlotLocator);
        if (!peg || !loc || !slot[0]) {
            continue;
        }

        float w[3], e[3] = {};
        if (!rs7s7d::SafeCopy(w, loc + kWorldPos, 12)) {
            continue;
        }

        GameEuler(loc + kWorldRot, e);
        GameSetVec(g_rs7_fn_setpos, peg, w);
        GameSetVec(g_rs7_fn_setrot, peg, e);
    }
}
}  // namespace

// The log lines of this module (exported: the emulator tests replace it with `ret`).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rs7_HeadLog(const char* fmt, const void* a,
                                                                               const void* b, const void* c) {
    try {
        Rs7Log(fmt, a, b, c);
    } catch (...) {
    }
}

// ---- ownership (review #1) ----
// The table is the only native identification: the stubs (Rs7_PegOwnedZF), IsNative and Disown scan it, nothing reads
// the PegSet to decide. Claims: compare-exchange 0 -> PegSet (PSC1 on resource-loader jobs, possibly several at
// once). Clears: PSA2, by the one thread freeing that PegSet, before the free, so a PegSet constructed at the same
// address afterwards claims a fresh entry. 0x59C300 (dtor) and the sized free 0x58DA05 have one caller each and
// run back to back (0x58D9F4 / 0x58DA0D), so every native PegSet's free passes PSA2.
static_assert(rs7head::kOwners == 64, "Rs7_PegOwnedZF (stubs.cpp, PSU1/PSU2/PSM1) scans 64 entries");

bool rs7head::Own(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    if (IsNative(pegset)) {
        return true;
    }

    volatile long* t = reinterpret_cast<volatile long*>(g_rs7_pegsets);

    for (unsigned i = 0; i < kOwners; ++i) {
        if (!g_rs7_pegsets[i] && _InterlockedCompareExchange(&t[i], long(a), 0) == 0) {
            return true;
        }
    }

    return false;
}

bool rs7head::Disown(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    volatile long* t = reinterpret_cast<volatile long*>(g_rs7_pegsets);

    for (unsigned i = 0; i < kOwners; ++i) {
        if (g_rs7_pegsets[i] == a && _InterlockedCompareExchange(&t[i], 0, long(a)) == long(a)) {
            return true;
        }
    }

    return false;
}

bool rs7head::IsNative(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    const volatile uintptr_t* t = g_rs7_pegsets;

    for (unsigned i = 0; i < kOwners; ++i) {
        if (t[i] == a) {
            return true;
        }
    }

    return false;
}

unsigned rs7head::OwnedCount() noexcept {
    unsigned n = 0;
    const volatile uintptr_t* t = g_rs7_pegsets;

    for (unsigned i = 0; i < kOwners; ++i) {
        n += t[i] != 0;
    }

    return n;
}

void rs7head::ResetOwners() noexcept {
    for (auto& e : g_rs7_pegsets) {
        e = 0;
    }
}

// ---- pure helpers ----

void rs7head::InitTail(uint8_t* ps) {
    uint8_t colours[kStockSlots][kColourStride];

    std::memcpy(colours, ps + kStockColours, sizeof colours);  // before slots 6/7 overwrite them

    for (int s = kStockSlots; s < kCapacity; ++s) {
        std::memcpy(Slot(ps, s), Slot(ps, 0), kSlotStride);
    }

    for (int i = 0; i < kCapacity; ++i) {
        std::memcpy(ps + kColours + uint32_t(i) * kColourStride, colours[i < kStockSlots ? i : 0], kColourStride);
    }

    ps[kTail] = uint8_t(kCapacity);
    ps[kTail + 1] = 0;
    ps[kTail + 2] = 0;
    ps[kTail + 3] = 0;
    *reinterpret_cast<uint32_t*>(ps + kMagicOff) = kMagic;
}

unsigned rs7head::LoadedCounts(uint8_t* ps) {
    unsigned count = *reinterpret_cast<uint16_t*>(ps + kPegCount);

    if (count > unsigned(kCapacity)) {
        count = kCapacity;
    }
    ps[kTail + 1] = uint8_t(count);

    if (count > unsigned(kStockSlots)) {
        count = kStockSlots;
        *reinterpret_cast<uint16_t*>(ps + kPegCount) = uint16_t(count);
    }

    return count;
}

bool rs7head::NamedColour(const uint8_t* mgr, uint32_t player, const float* fxRgb, float out[3]) {
    if (!mgr || player >= 2) {
        return false;
    }

    const uint32_t mode = *reinterpret_cast<const uint32_t*>(mgr + 0x348 + player * 4);
    if (mode >= 2) {
        return false;
    }

    const float* c1 = *reinterpret_cast<const float* const*>(mgr + rs7colour::PtrOffset(mode, 1));
    const float* c4 = *reinterpret_cast<const float* const*>(mgr + rs7colour::PtrOffset(mode, 4));
    if (!c1 || !c4) {
        return false;
    }

    auto mx = [](const float* c) {
        return c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    };
    const float k = mx(c1) > 0.0f ? mx(c4) / mx(c1) : 1.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = fxRgb[mode * 3 + i] * k;
    }

    return true;
}

bool rs7head::IsTunerNode(const char* name) {
    if (!name) {
        return false;
    }

    char low[48];
    size_t i = 0;

    for (; name[i] && i + 1 < sizeof low; ++i) {
        low[i] = char(std::tolower(static_cast<unsigned char>(name[i])));
    }

    low[i] = 0;

    return std::strstr(low, "arrow") || std::strstr(low, "letter");
}

// ---- stub entry points ----
extern "C" void __cdecl Rs7_PegSetCtor(uint8_t* ps) {
    if (!ps) {
        return;
    }

    // Registered before the layout changes; a full table leaves this PegSet entirely stock (6 slots,
    // pegColor at +0FCh): every stub then keeps the stock counts. Its free at PSA2 is then 15Ch for PSA1's
    // 1D4h block (HYPOTHESIS: harmless for the game allocator; only reachable with 64 live native PegSets).
    if (!Own(ps)) {
        _InterlockedIncrement(&g_ownFull);
        return;
    }

    InitTail(ps);
    _InterlockedIncrement(&g_ctors);
}

// PSC1 / PSL2 run inside the resource jobs of the guitar load: they only touch the PegSet and
// counters; N5 logs (LogLoaded). (The boot crash first blamed on a PSL2 log line overrunning a job
// stack was PSL2's old jmp site, which 0x59C587 jumped into: PSL2 is a call site now, stubs.cpp.)
extern "C" void __cdecl Rs7_PegSetLoaded(uint8_t* ps) {
    if (!IsNative(ps)) {
        _InterlockedIncrement(&g_foreign);
        return;
    }

    const unsigned loaded = At<uint16_t>(ps, kPegCount);

    LoadedCounts(ps);

    for (unsigned s = kStockSlots; s < loaded && s < unsigned(kCapacity); ++s) {
        GameShowEntity(At<uint8_t*>(Slot(ps, int(s)), kSlotEntity), 0);  // the take showed it (0x595B99)
    }

    if (loaded >= 7) {
        _InterlockedIncrement(&g_loads7);
    }
    LoadRec r{reinterpret_cast<uintptr_t>(ps), loaded, Slot(ps, kStockSlots)[0] ? 1u : 0u};

    g_loads.Push(r);
}

long rs7head::LoadRing::Reserve() {
    return _InterlockedIncrement(&reserve_) - 1;
}

void rs7head::LoadRing::Publish(long n, const LoadRec& r) {
    Entry& e = ring_[n & 7];

    _InterlockedExchange(&e.seq, 0);  // being written: a reader of the previous lap sees a torn entry
    e.rec = r;
    _InterlockedExchange(&e.seq, n + 1);  // payload first, publication last
}

rs7head::LoadRing::Result rs7head::LoadRing::Pop(LoadRec& out) {
    const long reserve = reserve_;
    if (done_ >= reserve) {
        return Empty;
    }

    if (reserve - done_ > 8) {  // the producers lapped the ring: skip to the oldest survivor
        dropped_ += reserve - 8 - done_;
        done_ = reserve - 8;
    }

    Entry& e = ring_[done_ & 7];
    const long s1 = e.seq;

    if (s1 != done_ + 1) {
        if (s1 > done_ + 1) {  // overwritten by a later lap
            ++dropped_;
            ++done_;
            return Dropped;
        }

        // reserved, payload not published yet: retry next frame
        return Empty;
    }

    const LoadRec copy = e.rec;

    if (e.seq != s1) {
        ++dropped_;
        ++done_;
        return Dropped;
    }  // torn by a producer of the next lap

    out = copy;
    ++done_;

    return Got;
}

namespace {
void LogLoads() {  // N5, game thread: the loads PSL2 recorded since the last frame
    LoadRec r;

    for (;;) {
        const LoadRing::Result res = g_loads.Pop(r);
        if (res == LoadRing::Empty) {
            break;
        }
        if (res == LoadRing::Dropped || !Upto(&g_logLoad, 8)) {
            continue;
        }

        Rs7_HeadLog(r.loaded >= 7 ? "headstock: PegSet %p loaded %p pegs natively (slot 6 %s), 6 active"
                                  : "headstock: PegSet %p loaded %p pegs (the instrument's count)",
                    reinterpret_cast<void*>(r.ps), reinterpret_cast<void*>(uintptr_t(r.loaded)),
                    r.slot6 ? "taken, on its Locator_Peg_7" : "not taken: no Locator_Peg_7 in the model");
    }

    if (g_foreign && Once(&g_logForeign)) {
        Rs7_HeadLog("headstock: %p PegSet load(s) WITHOUT the rs7 layout (constructed before the patch): stock, 6 pegs",
                    reinterpret_cast<void*>(uintptr_t(g_foreign)), nullptr, nullptr);
    }

    if (g_ownFull && Once(&g_logOwnFull)) {
        Rs7_HeadLog("headstock: PegSet ownership table FULL (%p PegSets left stock, %p owned): investigate a free that "
                    "bypasses PSA2",
                    reinterpret_cast<void*>(uintptr_t(g_ownFull)), reinterpret_cast<void*>(uintptr_t(OwnedCount())),
                    nullptr);
    }

    if (g_ctors && Once(&g_logCtor)) {
        Rs7_HeadLog("headstock: PegSets constructed natively so far: %p (8 slots, pegColor at +14Ch, size 1D4h)",
                    reinterpret_cast<void*>(uintptr_t(g_ctors)), nullptr, nullptr);
    }
}
}  // namespace

extern "C" void __cdecl Rs7_PegNamed(uint8_t* session, uint32_t index) {
    uint8_t* guitar = session ? At<uint8_t*>(session, 0x2C) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    if (!IsNative(ps) || index >= unsigned(kCapacity) || index >= At<uint16_t>(ps, kPegCount)) {
        return;
    }

    uint8_t* slot = Slot(ps, int(index));
    if (!slot[0]) {
        return;
    }

    float rgb[3];
    uint8_t* mgr = g_rs7_colormgr ? *reinterpret_cast<uint8_t**>(g_rs7_colormgr) : nullptr;
    if (!NamedColour(mgr, At<uint32_t>(session, 8), g_rs7_fx_rgb, rgb)) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        At<float>(slot, kSlotRgb + 4 * uint32_t(i)) = rgb[i];
    }

    At<uint32_t>(slot, kSlotNamed) = 2 + index;  // string colour index (2..7 = strings 0..5 at 0x59BF80)
}

void Rs7Head_TrackColourPlayer(uint8_t* session) {
    uint8_t* guitar = session ? At<uint8_t*>(session, 0x2C) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    const unsigned player = session ? At<uint32_t>(session, 8) : 2;

    if (IsNative(ps) && player < 2) {
        ps[kColourPlayer] = uint8_t(player);
    }
}

// ---- per frame / tuner ----
void Rs7Head_Frame(uint8_t* guitar, bool want) {
    if (!g_rs7_native_pegset) {
        return;
    }

    LogLoads();
    uint8_t* head = guitar ? At<uint8_t*>(guitar, kGuitarHead) : nullptr;
    uint8_t* ps = head ? At<uint8_t*>(head, kHeadPegs) : nullptr;
    if (!IsNative(ps) || !At<uint8_t>(ps, kPegLoaded)) {
        return;
    }

    const uint8_t* manager = g_rs7_colormgr ? *reinterpret_cast<uint8_t**>(g_rs7_colormgr) : nullptr;
    const unsigned changed = rs7pegcolour::Refresh(ps, manager, ps[kColourPlayer]);
    static unsigned colourLogs = 0;

    if (changed && colourLogs < 8) {
        ++colourLogs;
        Rs7Log("pegcolour: refreshed %u cached peg colour(s), player %u (manager %p, PegSet %p)", changed,
               unsigned(ps[kColourPlayer]), static_cast<const void*>(manager), static_cast<void*>(ps));

        for (unsigned s = 0; s < At<uint16_t>(ps, kPegCount); ++s) {
            uint8_t* slot = Slot(ps, int(s));

            if (slot[0] && At<uint32_t>(slot, kSlotNamed) >= 2) {
                Rs7Log("pegcolour: selected slot %u name %u RGB %.3f/%.3f/%.3f outline phase %.3f/%.3f", s,
                       At<uint32_t>(slot, kSlotNamed), At<float>(slot, kSlotRgb), At<float>(slot, kSlotRgb + 4),
                       At<float>(slot, kSlotRgb + 8), At<float>(slot, 0x20), At<float>(slot, 0x24));
            }
        }
    }

    // C-lite: a guitar built with 7 strings has 7 pegs for its whole life (raised once, after the load
    // completed: the peg-7 slot is loaded for every guitar but active, i.e. in the stock loops, only on these).
    if (want && Loaded(ps) >= 7 && At<uint16_t>(ps, kPegCount) == 6 && g_rs7_head_on) {
        Raise(ps, "N5: the guitar was built with 7 strings");
    }
    if (!want) {
        return;
    }

    Reload();
    if (g_cullName[0] || g_culled) {
        ApplyCull(At<uint8_t*>(head, kHeadFrame));
    }
    if (g_stretch != 1.0f || g_stretchApplied != 1.0f) {
        ApplyStretch(guitar, head, ps);
    }
}

uint8_t* Rs7Head_Peg(uint8_t* guitar, int s) {
    uint8_t* ps = PegSetOf(guitar);
    if (!ps || s < 0 || s >= kCapacity || Rd<uint16_t>(ps, kPegCount) <= unsigned(s)) {
        return nullptr;
    }
    if (s >= kStockSlots && !IsNative(ps)) {
        return nullptr;
    }

    uint8_t* slot = Slot(ps, s);

    return slot[0] ? At<uint8_t*>(slot, kSlotEntity) : nullptr;
}

void Rs7Head_SetLive(const char* cull, float stretch, int mode) {
    Live l{};

    for (int i = 0; i < 47 && cull && cull[i]; ++i) {
        l.cull[i] = cull[i];
    }

    l.st = stretch;
    l.mode = mode;

    while (_InterlockedCompareExchange(&g_liveSpin, 1, 0) != 0) {
        _mm_pause();
    }

    g_live = l;
    _InterlockedExchange(&g_liveSpin, 0);
    _InterlockedExchange(&g_liveOwned, 1);
    _InterlockedIncrement(&g_liveSeq);
}

void Rs7Head_Stats(Rs7HeadStats* out) {
    Rs7HeadStats st{};

    st.pegsets = int(g_ctors);
    st.loaded7 = int(g_loads7);
    st.active7 = int(g_active7);
    st.liveOwned = g_liveOwned != 0;
    *out = st;
}

void Rs7Head_SetIni(const wchar_t* path) {
    g_ini[0] = 0;
    if (path && std::wcslen(path) < MAX_PATH) {
        wcscpy_s(g_ini, path);
    }
}

void Rs7Head_ResetLog() {
    for (volatile long* f : {&g_logCtor, &g_logLoad, &g_logRaise, &g_logNoSlot, &g_logForeign}) {
        _InterlockedExchange(f, 0);
    }
}
