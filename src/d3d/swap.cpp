#include "d3d/swap.h"
#include "core/colournative.h"
#include "core/dxt.h"
#include "core/patch.h"
#include "core/ramp.h"
#include "core/texlist.h"
#include "d3d/iids.h"
#include "d3d/sehread.h"
#include "log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" HRESULT __stdcall Rs7_SetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
extern "C" HRESULT __stdcall Rs7_GetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out);

// Hot-patchable thunks: the first 5 bytes are relocatable whole instructions, so a trampoline hooker
// (RSMods' MemUtil::TrampHook reads vTable[65] from its dummy device) can hook this slot after us.
extern "C" __declspec(naked) void Rs7_Thunk_SetTexture() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_SetTextureHook
    }
}

extern "C" __declspec(naked) void Rs7_Thunk_GetTexture() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_GetTextureHook
    }
}

namespace {
constexpr unsigned kSlotGet = 64, kSlotSet = 65, kStages = 16, kMaxRetries = 5, kRetryMs = 2000;
using SetTex = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
using GetTex = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);

SetTex g_origSet;
GetTex g_origGet;
IDirect3DDevice9* g_dev;  // the device we hooked
void** g_vt;              // its vtable
volatile bool g_installed;
volatile bool g_deviceOk = true;  // false (sticky) once a candidate is bound on another device
Rs7SwapCfg g_cfg = {{0xC8C8C8, 0xC8C8C8}, false};
CRITICAL_SECTION* g_listLock;
uintptr_t g_d3d9Lo, g_d3d9Hi;
Rs7SwapValidator g_validator;
volatile bool g_injectFault;
volatile LONG g_retireReq;
volatile LONG g_recolourReq;  // Rs7Swap_RequestRecolour -> the render thread (OnSet)
volatile LONG g_recolourRgb[2] = {0xC8C8C8, 0xC8C8C8};
const Rs7SehReader g_mem{};

// Render thread only.
struct Bound {
    IDirect3DBaseTexture9* stock;
    IDirect3DBaseTexture9* ours;
} g_bound[kStages];

struct Copy {
    uint64_t hash;
    uint32_t fmt;
    IDirect3DTexture9* tex;
} g_copies[kRs7MaxCands];  // own a reference

unsigned g_copyCount;
Rs7Cand* g_processing;  // the candidate Process works on (fault handler)

struct Locked {
    IDirect3DTexture9* tex;
    UINT level;
} g_locked;  // a level Process holds locked

const char* FamilyName(Rs7Family f) {
    return f == Rs7Family::Note ? "note" : "additive";
}

// Rs7Log that never throws into the hook (std::string allocation inside Rs7Log).
void LogQuiet(const char* fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    try {
        Rs7Log("%s", buf);
    } catch (...) {
    }
}

void UpdateV6(const char* reason) {
    Rs7Ramp_UpdateV6(g_installed, g_deviceOk, reason);
}

// ---- revalidation (Batch B review): under the game's list lock, re-check the candidate, then take the
// reference while the engine cannot free the texture. No C++ objects here (SEH).
int EngineAcquire(const Rs7Cand& c, IDirect3DTexture9** out) {
    CRITICAL_SECTION* cs = g_listLock;
    if (!cs || !TryEnterCriticalSection(cs)) {
        return -1;  // never wait on the render thread
    }

    int r = 0;

    __try {
        if (Rs7Tex_Revalidate(g_mem, c.obj, reinterpret_cast<uintptr_t>(c.d3d), c.family, g_d3d9Lo, g_d3d9Hi)) {
            auto* base = static_cast<IDirect3DBaseTexture9*>(c.d3d);

            r = SUCCEEDED(base->QueryInterface(kRs7IID_Texture9, reinterpret_cast<void**>(out))) && *out ? 1 : 2;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = 0;
    }

    LeaveCriticalSection(cs);

    return r;
}

// ---- terminal states: set the state last, then apply the V6 rule (both after stock/ours are final).
void Refuse(Rs7Cand& c, const char* why) {
    LogQuiet("ramp: refuse %s d3d=%p '%s': %s", FamilyName(c.family), c.d3d, c.path, why);
    c.state = Rs7CandState::Refused;
    char r[160];

    std::snprintf(r, sizeof r, "%s '%s' refused", FamilyName(c.family), c.path);
    UpdateV6(r);
}

void RetryLater(Rs7Cand& c, const char* why) {
    if (++c.retries >= kMaxRetries) {
        char w[160];

        std::snprintf(w, sizeof w, "%s (%u tries)", why, c.retries);
        Refuse(c, w);
        return;
    }

    LogQuiet("ramp: retry %s d3d=%p '%s' in %u ms (%u/%u): %s", FamilyName(c.family), c.d3d, c.path, kRetryMs,
             c.retries, kMaxRetries, why);
    c.retryAtMs = GetTickCount() + kRetryMs;
    c.state = Rs7CandState::Retry;
    char r[160];

    std::snprintf(r, sizeof r, "%s '%s' retrying", FamilyName(c.family), c.path);
    UpdateV6(r);
}

// LockRect/UnlockRect with the level remembered, so the fault handler can unlock it.
bool LockLevel(IDirect3DTexture9* t, UINT k, D3DLOCKED_RECT* lr, DWORD flags) {
    if (FAILED(t->LockRect(k, lr, nullptr, flags))) {
        return false;
    }

    g_locked = {t, k};

    return true;
}

void UnlockLevel(IDirect3DTexture9* t, UINT k) {
    g_locked = {};
    t->UnlockRect(k);
}

bool ReadLevels(IDirect3DTexture9* t, uint32_t fmt, std::vector<Rs7Image>& out, char* why, size_t whyLen) {
    for (UINT k = 0; k < out.size(); ++k) {
        D3DSURFACE_DESC d{};
        D3DLOCKED_RECT lr{};

        if (FAILED(t->GetLevelDesc(k, &d))) {
            std::snprintf(why, whyLen, "GetLevelDesc level %u failed", k);
            return false;
        }

        Rs7Image& im = out[k];

        im.w = d.Width;
        im.h = d.Height;
        im.px.assign(size_t(im.w) * im.h, 0);
        HRESULT hr = t->LockRect(k, &lr, nullptr, D3DLOCK_READONLY);

        if (FAILED(hr)) {
            std::snprintf(why, whyLen, "LockRect level %u: %08lX", k, hr);
            return false;
        }

        g_locked = {t, k};
        bool ok = true;
        const auto* src = static_cast<const uint8_t*>(lr.pBits);

        if (fmt == kRs7FmtDxt1 || fmt == kRs7FmtDxt5) {
            const size_t blockRows = (im.h + 3) / 4;

            ok = Rs7_DecodeDxt(src, size_t(lr.Pitch) * blockRows, size_t(lr.Pitch), im.w, im.h, fmt == kRs7FmtDxt5,
                               im.px.data());
            if (!ok) {
                std::snprintf(why, whyLen, "DXT decode level %u failed", k);
            }
        } else {
            for (unsigned y = 0; y < im.h; ++y) {
                std::memcpy(&im.px[size_t(y) * im.w], src + size_t(y) * lr.Pitch, size_t(im.w) * 4);
            }
        }

        UnlockLevel(t, k);
        if (!ok) {
            return false;
        }
    }

    return true;
}

// Writes `b` into `ours` level by level, then reads it back and compares (the runtime self-check also
// covers what the texture really holds). False: why.
bool WriteAndCheck(IDirect3DTexture9* ours, const std::vector<Rs7Image>& b, char* why, size_t whyLen) {
    for (UINT k = 0; k < b.size(); ++k) {
        D3DLOCKED_RECT lr{};

        if (!LockLevel(ours, k, &lr, 0)) {
            std::snprintf(why, whyLen, "LockRect (write) level %u failed", k);
            return false;
        }

        auto* dst = static_cast<uint8_t*>(lr.pBits);

        for (unsigned y = 0; y < b[k].h; ++y) {
            std::memcpy(dst + size_t(y) * lr.Pitch, &b[k].px[size_t(y) * b[k].w], size_t(b[k].w) * 4);
        }

        UnlockLevel(ours, k);
    }

    for (UINT k = 0; k < b.size(); ++k) {
        D3DLOCKED_RECT lr{};

        if (!LockLevel(ours, k, &lr, D3DLOCK_READONLY)) {
            std::snprintf(why, whyLen, "LockRect (read back) level %u failed", k);
            return false;
        }

        const auto* src = static_cast<const uint8_t*>(lr.pBits);
        bool same = true;

        for (unsigned y = 0; y < b[k].h && same; ++y) {
            same = std::memcmp(src + size_t(y) * lr.Pitch, &b[k].px[size_t(y) * b[k].w], size_t(b[k].w) * 4) == 0;
        }

        UnlockLevel(ours, k);

        if (!same) {
            std::snprintf(why, whyLen, "read-back of level %u differs", k);
            return false;
        }
    }

    return true;
}

// Render thread; c is Busy (claimed). Ends in Active, Refused, Retry, Retired or back in `prev`.
void Process(IDirect3DDevice9* dev, Rs7Cand& c, Rs7CandState prev) {
    if (g_injectFault) {
        RaiseException(0xE0000001, 0, 0, nullptr);
    }
    IDirect3DTexture9* t = nullptr;
    const int v = g_validator ? g_validator(c, &t) : EngineAcquire(c, &t);

    if (v < 0) {  // the list lock is busy: try again at the next bind, pass-through meanwhile
        c.state = prev;
        return;
    }

    if (v == 0) {
        LogQuiet("ramp: drop stale %s d3d=%p '%s': no longer that engine texture", FamilyName(c.family), c.d3d, c.path);
        Rs7Cand_Retire(&c, c.d3d, true);
        UpdateV6("stale candidate dropped");
        return;
    }

    if (v != 1 || !t) {
        Refuse(c, "not a 2D texture");
        return;
    }

    char why[160] = {};
    D3DSURFACE_DESC d{};

    if (FAILED(t->GetLevelDesc(0, &d))) {
        t->Release();
        Refuse(c, "GetLevelDesc(0) failed");
        return;
    }

    const unsigned levels = t->GetLevelCount();
    const Rs7RampPlan plan =
        Rs7Ramp_Plan({uint32_t(d.Format), d.Width, d.Height, levels, uint32_t(d.Pool), d.Usage}, g_cfg.allowInexact);

    if (plan.verdict == Rs7RampVerdict::Refuse) {
        std::snprintf(why, sizeof why, "%s (fmt=%u %ux%u levels=%u pool=%u usage=%08lX)", plan.why, unsigned(d.Format),
                      d.Width, d.Height, levels, unsigned(d.Pool), d.Usage);
        t->Release();
        Refuse(c, why);
        return;
    }

    std::vector<Rs7Image> stock(levels);

    if (!ReadLevels(t, uint32_t(d.Format), stock, why, sizeof why)) {
        t->Release();
        Refuse(c, why);
        return;
    }

    const uint64_t hash = Rs7Ramp_Hash(stock);
    const Rs7RowStats rs = Rs7Ramp_RowStats(stock[0]);

    LogQuiet("ramp: process %s d3d=%p '%s': fmt=%u %ux%u levels=%u pool=%u usage=%08lX hash=%016llX "
             "non-uniform-rows=%u grey7=%d grey15=%d",
             FamilyName(c.family), c.d3d, c.path, unsigned(d.Format), d.Width, d.Height, levels, unsigned(d.Pool),
             d.Usage, static_cast<unsigned long long>(hash), rs.nonUniformRows, int(rs.grey7), int(rs.grey15));

    IDirect3DTexture9* ours = nullptr;

    for (unsigned i = 0; i < g_copyCount; ++i) {
        if (g_copies[i].hash == hash && g_copies[i].fmt == plan.outFormat) {
            ours = g_copies[i].tex;
            ours->AddRef();
            LogQuiet("ramp: shared copy %p (hash %016llX) for %s d3d=%p", static_cast<void*>(ours),
                     static_cast<unsigned long long>(hash), FamilyName(c.family), c.d3d);
            break;
        }
    }

    if (!ours) {
        const uint32_t rgb[2] = {g_cfg.rgb[0], g_cfg.rgb[1]};
        const Rs7RampBuild b = Rs7Ramp_Build(stock, rgb);
        const Rs7RampCheck chk = Rs7Ramp_Verify(stock, b, rgb);

        if ((plan.exactPossible && !chk.exact) || !chk.v6Pure) {
            std::snprintf(why, sizeof why, "self-check failed: exact=%d v6-pure=%d %s", int(chk.exact), int(chk.v6Pure),
                          chk.why.c_str());
            t->Release();
            Refuse(c, why);
            return;
        }

        const HRESULT hr = dev->CreateTexture(d.Width, d.Height, levels, 0, D3DFORMAT(plan.outFormat), D3DPOOL_MANAGED,
                                              &ours, nullptr);

        if (FAILED(hr) || !ours) {
            std::snprintf(why, sizeof why, "CreateTexture: %08lX", hr);
            t->Release();
            RetryLater(c, why);
            return;
        }

        if (!WriteAndCheck(ours, b.levels, why, sizeof why)) {
            ours->Release();
            t->Release();
            Refuse(c, why);
            return;
        }

        LogQuiet("ramp: built %p (hash %016llX): %ux%u fmt=%u levels=%u, custom colour on levels 0-%u, "
                 "self-check exact=%d v6-pure=%d changed-lines=%u",
                 static_cast<void*>(ours), static_cast<unsigned long long>(hash), d.Width, d.Height, plan.outFormat,
                 levels, b.customLevels ? b.customLevels - 1 : 0, int(chk.exact), int(chk.v6Pure), chk.changedLines);
        if (levels > b.customLevels) {
            LogQuiet("ramp: note: levels %u-%u show string 6 as a row 6/7 blend (distance)", b.customLevels,
                     levels - 1);
        }

        if (g_copyCount < kRs7MaxCands) {
            ours->AddRef();  // the copies table keeps its own reference (sharing after a retire)
            g_copies[g_copyCount++] = {hash, plan.outFormat, ours};
        }
    }

    c.stock = t;  // our reference: the address cannot be reused while we bind a copy for it
    c.ours = ours;
    c.hash = hash;
    c.retries = 0;
    c.state = Rs7CandState::Active;  // last: stock/ours are complete
    LogQuiet("ramp: active %s d3d=%p '%s' -> copy %p", FamilyName(c.family), c.d3d, c.path, static_cast<void*>(ours));
    char r[160];

    std::snprintf(r, sizeof r, "%s '%s' active", FamilyName(c.family), c.path);
    UpdateV6(r);
}

// Render thread: release Active entries the worker flagged gone (the engine dropped their texture).
void RetireGone() {
    InterlockedExchange(&g_retireReq, 0);
    bool any = false;

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        if (!c || c->state != Rs7CandState::Active || c->gone < 2) {
            continue;
        }

        void* const d3d = c->d3d;
        auto* stock = static_cast<IDirect3DTexture9*>(c->stock);
        auto* ours = static_cast<IDirect3DTexture9*>(c->ours);

        for (auto& b : g_bound) {
            if (b.stock == stock) {
                b = {};
            }
        }

        LogQuiet("ramp: release %s d3d=%p '%s': gone from the engine list", FamilyName(c->family), d3d, c->path);

        if (Rs7Cand_Retire(c, d3d, true)) {
            if (ours) {
                ours->Release();
            }
            if (stock) {
                stock->Release();
            }
            any = true;
        }
    }

    if (any) {
        UpdateV6("gone textures released");
    }
}

// Render thread: repaint string 6's lines of every live copy with the requested colours (overlay,
// [color] string6_color / _cb live). Each Active candidate's copy is rebuilt from its stock texture
// exactly as Process built it (same Build + self-check); cached copies no candidate uses are dropped
// (they hold the old colour). A copy that fails is logged and keeps what it had.
void Recolour() {
    InterlockedExchange(&g_recolourReq, 0);
    const uint32_t rgb[2] = {uint32_t(g_recolourRgb[0]) & 0xFFFFFF, uint32_t(g_recolourRgb[1]) & 0xFFFFFF};
    if (rgb[0] == g_cfg.rgb[0] && rgb[1] == g_cfg.rgb[1]) {
        return;
    }

    g_cfg.rgb[0] = rgb[0];
    g_cfg.rgb[1] = rgb[1];
    IDirect3DTexture9* done[kRs7MaxCands] = {};
    unsigned nDone = 0, failed = 0;

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        if (!c || c->state != Rs7CandState::Active || !c->stock || !c->ours) {
            continue;
        }

        auto* stock = static_cast<IDirect3DTexture9*>(c->stock);
        auto* ours = static_cast<IDirect3DTexture9*>(c->ours);
        bool seen = false;

        for (unsigned k = 0; k < nDone; ++k) {
            seen = seen || done[k] == ours;
        }

        if (seen) {
            continue;
        }

        char why[160] = {};
        D3DSURFACE_DESC d{};
        bool ok = SUCCEEDED(stock->GetLevelDesc(0, &d));
        std::vector<Rs7Image> lv(ok ? stock->GetLevelCount() : 0);

        ok = ok && ReadLevels(stock, uint32_t(d.Format), lv, why, sizeof why);

        if (ok) {
            const Rs7RampPlan plan =
                Rs7Ramp_Plan({uint32_t(d.Format), d.Width, d.Height, unsigned(lv.size()), uint32_t(d.Pool), d.Usage},
                             g_cfg.allowInexact);
            const Rs7RampBuild b = Rs7Ramp_Build(lv, rgb);
            const Rs7RampCheck chk = Rs7Ramp_Verify(lv, b, rgb);

            if ((plan.exactPossible && !chk.exact) || !chk.v6Pure) {
                std::snprintf(why, sizeof why, "self-check failed: exact=%d v6-pure=%d %s", int(chk.exact),
                              int(chk.v6Pure), chk.why.c_str());
                ok = false;
            } else {
                ok = WriteAndCheck(ours, b.levels, why, sizeof why);
            }
        }

        if (!ok) {
            ++failed;
            LogQuiet("ramp: recolour of copy %p ('%s') failed: %s", static_cast<void*>(ours), c->path, why);
        }

        if (nDone < kRs7MaxCands) {
            done[nDone++] = ours;
        }
    }

    unsigned dropped = 0;

    for (unsigned i = 0; i < g_copyCount;) {
        bool used = false;

        for (unsigned k = 0; k < nDone; ++k) {
            used = used || done[k] == g_copies[i].tex;
        }

        if (used) {
            ++i;
            continue;
        }

        g_copies[i].tex->Release();
        g_copies[i] = g_copies[--g_copyCount];
        ++dropped;
    }

    LogQuiet("ramp: recoloured %u copies to %06X / %06X (%u failed, %u unused cached copies dropped)", nDone - failed,
             unsigned(rgb[0]), unsigned(rgb[1]), failed, dropped);
}

IDirect3DBaseTexture9* OnSet(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (stage < kStages) {
        g_bound[stage] = {};
    }
    if (g_retireReq) {
        RetireGone();
    }
    if (g_recolourReq && dev == g_dev) {
        Recolour();
    }
    rs7colour::DrainPending();  // review #5: publishes only if this is the manager's (CMBUILD) thread
    if (!tex || !Rs7Cand_Count()) {
        return tex;
    }

    Rs7Cand* c = Rs7Cand_Find(tex);
    if (!c) {
        return tex;
    }

    if (dev != g_dev) {
        if (g_deviceOk) {
            g_deviceOk = false;
            LogQuiet("ramp: candidate d3d=%p bound on device %p, hooked %p: pass-through, swapping off", tex,
                     static_cast<void*>(dev), static_cast<void*>(g_dev));
        }

        UpdateV6("SetTexture on another device");
        return tex;
    }

    const Rs7CandState s = c->state;

    if ((s == Rs7CandState::Pending || (s == Rs7CandState::Retry && int32_t(GetTickCount() - c->retryAtMs) >= 0)) &&
        Rs7Cand_Claim(c, tex)) {
        g_processing = c;
        Process(dev, *c, s);  // render thread, the game is binding it now
        g_processing = nullptr;
    }

    if (c->state != Rs7CandState::Active || c->d3d != tex) {
        return tex;
    }

    auto* ours = static_cast<IDirect3DBaseTexture9*>(c->ours);

    if (stage < kStages) {
        g_bound[stage] = {tex, ours};
    }
    Rs7Ramp_CountSwap();

    return ours;
}

// After a fault inside OnSet: unlock what Process left locked, refuse the candidate it worked on.
void AfterFault() {
    Rs7Ramp_CountFault();

    if (g_locked.tex) {
        __try {
            g_locked.tex->UnlockRect(g_locked.level);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        g_locked = {};
    }

    Rs7Cand* c = g_processing;

    g_processing = nullptr;
    if (c && c->state == Rs7CandState::Busy) {
        Refuse(*c, "fault while processing");
    }
}

// Describes where a d3d9 entry point really goes: "d3d9.dll", or "inline-hooked -> <module>".
uintptr_t JumpTarget(uintptr_t fn) {
    uint8_t b[6] = {};
    if (!g_mem.Read(fn, b, sizeof b)) {
        return 0;
    }

    if (b[0] == 0xE9) {
        int32_t r;

        std::memcpy(&r, b + 1, 4);
        return fn + 5 + r;
    }

    if (b[0] == 0xEB) {
        return fn + 2 + int8_t(b[1]);
    }

    if (b[0] == 0xFF && b[1] == 0x25) {
        uint32_t slot, dest = 0;

        std::memcpy(&slot, b + 2, 4);
        return g_mem.Read(slot, &dest, 4) ? dest : 0;
    }

    return 0;
}

void ModuleName(uintptr_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = {};

    if (!a ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(a), &m) ||
        !GetModuleFileNameA(m, path, MAX_PATH)) {
        std::snprintf(out, n, "no module");
        return;
    }

    const char* base = path;

    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    std::snprintf(out, n, "%s", base);
}

void DescribeBody(uintptr_t fn, char* out, size_t n) {
    char mod[MAX_PATH];
    const uintptr_t to = JumpTarget(fn);

    ModuleName(to ? to : fn, mod, sizeof mod);
    if (to) {
        std::snprintf(out, n, "inline-hooked -> %s", mod);
    } else {
        std::snprintf(out, n, "%s", mod);
    }
}
}  // namespace

extern "C" HRESULT __stdcall Rs7_SetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    IDirect3DBaseTexture9* bind = tex;

    __try {
        bind = OnSet(dev, stage, tex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bind = tex;
        AfterFault();
    }

    return g_origSet(dev, stage, bind);
}

// Hide the swap: whoever asks (RSMods' CaptureNoteColorRamp / stem CRCs) gets the stock texture.
extern "C" HRESULT __stdcall Rs7_GetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out) {
    const HRESULT hr = g_origGet(dev, stage, out);

    __try {
        if (SUCCEEDED(hr) && out && *out && dev == g_dev && stage < kStages && g_bound[stage].ours == *out) {
            (*out)->Release();  // cannot free: the candidate holds a reference
            *out = g_bound[stage].stock;
            (*out)->AddRef();
            Rs7Ramp_CountHidden();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Rs7Ramp_CountFault();
    }

    return hr;
}

void Rs7Swap_Configure(const Rs7SwapCfg& c) {
    g_cfg = c;
    InterlockedExchange(&g_recolourRgb[0], LONG(c.rgb[0]));
    InterlockedExchange(&g_recolourRgb[1], LONG(c.rgb[1]));
}

void Rs7Swap_RequestRecolour(const uint32_t rgb[2]) {
    InterlockedExchange(&g_recolourRgb[0], LONG(rgb[0] & 0xFFFFFF));
    InterlockedExchange(&g_recolourRgb[1], LONG(rgb[1] & 0xFFFFFF));
    InterlockedExchange(&g_recolourReq, 1);  // last: the render thread reads the colours after it
}

void Rs7Swap_Stats(Rs7SwapStats* out) {
    Rs7SwapStats st{};

    st.installed = g_installed;
    st.deviceOk = g_deviceOk;
    st.copies = g_copyCount;
    st.rgb[0] = g_cfg.rgb[0];
    st.rgb[1] = g_cfg.rgb[1];

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        const Rs7Cand* c = Rs7Cand_At(i);
        if (!c || !c->d3d) {
            continue;
        }

        switch (c->state) {
            case Rs7CandState::Active:
                ++st.active;
                break;
            case Rs7CandState::Refused:
                ++st.refused;
                break;
            case Rs7CandState::Retry:
                ++st.retry;
                break;
            default:
                ++st.pending;
                break;
        }
    }

    *out = st;
}

void Rs7Swap_SetEngine(CRITICAL_SECTION* listLock, uintptr_t lo, uintptr_t hi) {
    g_d3d9Lo = lo;
    g_d3d9Hi = hi;
    g_listLock = listLock;
}

bool Rs7Swap_Installed() {
    return g_installed;
}

void Rs7Swap_UpdateV6(const char* reason) {
    UpdateV6(reason);
}

void Rs7Swap_RequestRetire() {
    InterlockedExchange(&g_retireReq, 1);
}

bool Rs7Swap_InstallOn(void* device) {
    if (!device) {
        return false;
    }

    void** vt = *static_cast<void***>(device);
    void* const thSet = reinterpret_cast<void*>(&Rs7_Thunk_SetTexture);
    void* const thGet = reinterpret_cast<void*>(&Rs7_Thunk_GetTexture);
    if (vt[kSlotSet] == thSet && vt[kSlotGet] == thGet) {
        return true;  // idempotent
    }

    if (g_installed) {
        LogQuiet("ramp: device %p has another vtable %p (hooked %p): not hooked", device, static_cast<void*>(vt),
                 static_cast<void*>(g_vt));
        return false;
    }

    char bodySet[MAX_PATH + 32], bodyGet[MAX_PATH + 32];

    DescribeBody(reinterpret_cast<uintptr_t>(vt[kSlotSet]), bodySet, sizeof bodySet);
    DescribeBody(reinterpret_cast<uintptr_t>(vt[kSlotGet]), bodyGet, sizeof bodyGet);
    g_origGet = reinterpret_cast<GetTex>(vt[kSlotGet]);
    g_origSet = reinterpret_cast<SetTex>(vt[kSlotSet]);
    g_dev = static_cast<IDirect3DDevice9*>(device);
    g_vt = vt;
    g_deviceOk = true;
    uintptr_t old = 0;

    // GetTexture first: once SetTexture can swap, GetTexture already hides it.
    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotGet]), reinterpret_cast<uintptr_t>(thGet), &old)) {
        LogQuiet("ramp: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotGet);
        g_dev = nullptr;
        g_vt = nullptr;
        return false;
    }

    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotSet]), reinterpret_cast<uintptr_t>(thSet), &old)) {
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotGet]), reinterpret_cast<uintptr_t>(g_origGet), &old);
        LogQuiet("ramp: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotSet);
        g_dev = nullptr;
        g_vt = nullptr;
        return false;
    }

    g_installed = true;
    LogQuiet("ramp: device %p vtable %p: SetTexture slot %p (body %s) GetTexture slot %p (body %s) -> hooked; "
             "RSMods (xinput1_3.dll) %s",
             device, static_cast<void*>(vt), reinterpret_cast<void*>(g_origSet), bodySet,
             reinterpret_cast<void*>(g_origGet), bodyGet, GetModuleHandleW(L"xinput1_3.dll") ? "present" : "absent");

    return true;
}

void Rs7Swap_Recheck() {
    static bool foreignLogged = false;
    if (!g_installed || !g_vt) {
        return;
    }

    uintptr_t slots[2] = {};
    if (!g_mem.Read(reinterpret_cast<uintptr_t>(&g_vt[kSlotGet]), slots, sizeof slots)) {
        return;
    }

    const uintptr_t thGet = reinterpret_cast<uintptr_t>(&Rs7_Thunk_GetTexture);
    const uintptr_t thSet = reinterpret_cast<uintptr_t>(&Rs7_Thunk_SetTexture);
    if (slots[0] == thGet && slots[1] == thSet) {
        return;
    }

    if (slots[0] == reinterpret_cast<uintptr_t>(g_origGet) && slots[1] == reinterpret_cast<uintptr_t>(g_origSet)) {
        uintptr_t old = 0;
        const bool ok = Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotGet]), thGet, &old) &&
                        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotSet]), thSet, &old);

        LogQuiet("ramp: device %p: slots were reset to d3d9's entry points - %s", static_cast<void*>(g_dev),
                 ok ? "hooked again" : "re-hook FAILED");
        return;
    }

    if (!foreignLogged) {
        foreignLogged = true;
        LogQuiet("ramp: device %p: slot 64/65 now %08X/%08X (not ours, not d3d9's): left alone",
                 static_cast<void*>(g_dev), unsigned(slots[0]), unsigned(slots[1]));
    }
}

void Rs7Swap_UninstallForTests() {
    if (g_vt) {
        uintptr_t old = 0;

        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotSet]), reinterpret_cast<uintptr_t>(g_origSet), &old);
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotGet]), reinterpret_cast<uintptr_t>(g_origGet), &old);
    }

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        if (!c || c->state != Rs7CandState::Active) {
            continue;
        }

        if (c->ours) {
            static_cast<IDirect3DTexture9*>(c->ours)->Release();
        }
        if (c->stock) {
            static_cast<IDirect3DTexture9*>(c->stock)->Release();
        }
        c->ours = c->stock = nullptr;
        c->state = Rs7CandState::Refused;
    }

    for (unsigned i = 0; i < g_copyCount; ++i) {
        g_copies[i].tex->Release();
    }

    g_copyCount = 0;

    for (auto& b : g_bound) {
        b = {};
    }

    g_vt = nullptr;
    g_dev = nullptr;
    g_origSet = nullptr;
    g_origGet = nullptr;
    g_installed = false;
    g_deviceOk = true;
    g_injectFault = false;
    g_retireReq = 0;
    g_recolourReq = 0;
    g_processing = nullptr;
    g_locked = {};
}

HRESULT Rs7Swap_RawGetTextureForTests(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out) {
    return g_origGet ? g_origGet(dev, stage, out) : dev->GetTexture(stage, out);
}

void Rs7Swap_InjectFaultForTests(bool on) {
    g_injectFault = on;
}

void Rs7Swap_SetValidatorForTests(Rs7SwapValidator v) {
    g_validator = v;
}
