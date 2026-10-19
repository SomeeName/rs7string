// The stock colour ramps of the swap: the render thread processes each candidate the worker found (copy of
// the ramp with string 6's lines repainted, after a self-check), binds the copy in its place, repaints the
// copies when the colour changes, releases what the engine dropped, and refuses what it cannot copy.
#include "d3d/swap_ramp.h"
#include "d3d/hookutil.h"
#include "d3d/iids.h"
#include "d3d/swap_foreign.h"
#include "d3d/swap_hook.h"
#include "d3d/swap_texio.h"
#include "d3d/texlist.h"
#include "features/colours/colours.h"
#include <cstdio>
#include <vector>

namespace rs7swap {
RampState g_ramp;
}  // namespace rs7swap

namespace {
using rs7d3d::LogQuiet;
using namespace rs7swap;
constexpr unsigned kMaxRetries = 5, kRetryMs = 2000;
enum class Step { Ok, Refuse, Retry };

const char* FamilyName(Rs7Family f) {
    return f == Rs7Family::Note ? "note" : "additive";
}

// The V6 rule after `what` happened to a candidate: "<family> '<path>' <what>".
void UpdateV6For(const Rs7Cand& c, const char* what) {
    char r[160];

    std::snprintf(r, sizeof r, "%s '%s' %s", FamilyName(c.family), c.path, what);
    Rs7Swap_UpdateV6(r);
}

// ---- revalidation (Batch B review): under the game's list lock, re-check the candidate, then take the
// reference while the engine cannot free the texture. No C++ objects here (SEH).
int EngineAcquire(const Rs7Cand& c, IDirect3DTexture9** out) {
    CRITICAL_SECTION* cs = g_ramp.listLock;
    if (!cs || !TryEnterCriticalSection(cs)) {
        return -1;  // never wait on the render thread
    }

    int r = 0;

    __try {
        if (Rs7Tex_Revalidate(g_hook.mem, c.obj, reinterpret_cast<uintptr_t>(c.d3d), c.family, g_ramp.d3d9Lo,
                              g_ramp.d3d9Hi)) {
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
    UpdateV6For(c, "refused");
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
    UpdateV6For(c, "retrying");
}

// ---- the copy cache: identical stock ramps (same hash and format) share one copy.
// A new reference on the cached copy, or nullptr.
IDirect3DTexture9* SharedCopy(uint64_t hash, uint32_t fmt) {
    for (unsigned i = 0; i < g_ramp.copyCount; ++i) {
        if (g_ramp.copies[i].hash == hash && g_ramp.copies[i].fmt == fmt) {
            g_ramp.copies[i].tex->AddRef();

            return g_ramp.copies[i].tex;
        }
    }

    return nullptr;
}

void ReleaseBoth(void* ours, void* stock) {
    if (ours) {
        static_cast<IDirect3DTexture9*>(ours)->Release();
    }
    if (stock) {
        static_cast<IDirect3DTexture9*>(stock)->Release();
    }
}

// ---- Process: stock texture t -> our copy (shared or built). On failure why is set.
struct Plan {
    D3DSURFACE_DESC d;
    unsigned levels;
    Rs7RampPlan plan;
};

Step BuildNew(IDirect3DDevice9* dev, const Plan& p, const std::vector<Rs7Image>& stock, uint64_t hash,
              IDirect3DTexture9** ours, char* why, size_t whyLen) {
    CheckedBuild cb;

    if (!BuildChecked(stock, g_ramp.cfg.rgb, p.plan.exactPossible, cb, why, whyLen)) {
        return Step::Refuse;
    }

    switch (MakeCopy(dev, p.d, p.levels, D3DFORMAT(p.plan.outFormat), cb.build.levels, ours, why, whyLen)) {
        case CopyResult::CreateFailed:
            return Step::Retry;
        case CopyResult::WriteFailed:
            return Step::Refuse;
        default:
            break;
    }

    LogQuiet("ramp: built %p (hash %016llX): %ux%u fmt=%u levels=%u, custom colour on levels 0-%u, "
             "self-check exact=%d v6-pure=%d changed-lines=%u",
             static_cast<void*>(*ours), static_cast<unsigned long long>(hash), p.d.Width, p.d.Height, p.plan.outFormat,
             p.levels, cb.build.customLevels ? cb.build.customLevels - 1 : 0, int(cb.chk.exact), int(cb.chk.v6Pure),
             cb.chk.changedLines);
    if (p.levels > cb.build.customLevels) {
        LogQuiet("ramp: note: levels %u-%u show string 6 as a row 6/7 blend (distance)", cb.build.customLevels,
                 p.levels - 1);
    }

    if (g_ramp.copyCount < kRs7MaxCands) {
        (*ours)->AddRef();  // the copies table keeps its own reference (sharing after a retire)
        g_ramp.copies[g_ramp.copyCount++] = {hash, p.plan.outFormat, *ours};
    }

    return Step::Ok;
}

Step PrepareCopy(IDirect3DDevice9* dev, const Rs7Cand& c, IDirect3DTexture9* t, IDirect3DTexture9** ours,
                 uint64_t* hash, char* why, size_t whyLen) {
    Plan p{};

    if (FAILED(t->GetLevelDesc(0, &p.d))) {
        std::snprintf(why, whyLen, "GetLevelDesc(0) failed");
        return Step::Refuse;
    }

    const D3DSURFACE_DESC& d = p.d;

    p.levels = t->GetLevelCount();
    p.plan = Rs7Ramp_Plan({uint32_t(d.Format), d.Width, d.Height, p.levels, uint32_t(d.Pool), d.Usage},
                          g_ramp.cfg.allowInexact);

    if (p.plan.verdict == Rs7RampVerdict::Refuse) {
        std::snprintf(why, whyLen, "%s (fmt=%u %ux%u levels=%u pool=%u usage=%08lX)", p.plan.why, unsigned(d.Format),
                      d.Width, d.Height, p.levels, unsigned(d.Pool), d.Usage);
        return Step::Refuse;
    }

    std::vector<Rs7Image> stock(p.levels);

    if (!ReadLevels(t, uint32_t(d.Format), stock, why, whyLen)) {
        return Step::Refuse;
    }

    *hash = Rs7Ramp_Hash(stock);
    const Rs7RowStats rs = Rs7Ramp_RowStats(stock[0]);

    LogQuiet("ramp: process %s d3d=%p '%s': fmt=%u %ux%u levels=%u pool=%u usage=%08lX hash=%016llX "
             "non-uniform-rows=%u grey7=%d grey15=%d",
             FamilyName(c.family), c.d3d, c.path, unsigned(d.Format), d.Width, d.Height, p.levels, unsigned(d.Pool),
             d.Usage, static_cast<unsigned long long>(*hash), rs.nonUniformRows, int(rs.grey7), int(rs.grey15));

    *ours = SharedCopy(*hash, p.plan.outFormat);

    if (*ours) {
        LogQuiet("ramp: shared copy %p (hash %016llX) for %s d3d=%p", static_cast<void*>(*ours),
                 static_cast<unsigned long long>(*hash), FamilyName(c.family), c.d3d);
        return Step::Ok;
    }

    return BuildNew(dev, p, stock, *hash, ours, why, whyLen);
}

// Render thread; c is Busy (claimed). Ends in Active, Refused, Retry, Retired or back in `prev`.
void Process(IDirect3DDevice9* dev, Rs7Cand& c, Rs7CandState prev) {
    if (g_ramp.injectFault) {
        RaiseException(0xE0000001, 0, 0, nullptr);
    }
    IDirect3DTexture9* t = nullptr;
    const int v = g_ramp.validator ? g_ramp.validator(c, &t) : EngineAcquire(c, &t);

    if (v < 0) {  // the list lock is busy: try again at the next bind, pass-through meanwhile
        c.state = prev;
        return;
    }

    if (v == 0) {
        LogQuiet("ramp: drop stale %s d3d=%p '%s': no longer that engine texture", FamilyName(c.family), c.d3d, c.path);
        Rs7Cand_Retire(&c, c.d3d, true);
        Rs7Swap_UpdateV6("stale candidate dropped");
        return;
    }

    if (v != 1 || !t) {
        Refuse(c, "not a 2D texture");
        return;
    }

    char why[160] = {};
    IDirect3DTexture9* ours = nullptr;
    uint64_t hash = 0;
    const Step s = PrepareCopy(dev, c, t, &ours, &hash, why, sizeof why);

    if (s != Step::Ok) {
        t->Release();
        if (s == Step::Retry) {
            RetryLater(c, why);
        } else {
            Refuse(c, why);
        }

        return;
    }

    c.stock = t;  // our reference: the address cannot be reused while we bind a copy for it
    c.ours = ours;
    c.hash = hash;
    c.retries = 0;
    c.state = Rs7CandState::Active;  // last: stock/ours are complete
    LogQuiet("ramp: active %s d3d=%p '%s' -> copy %p", FamilyName(c.family), c.d3d, c.path, static_cast<void*>(ours));
    UpdateV6For(c, "active");
}

// Render thread: release Active entries the worker flagged gone (the engine dropped their texture).
void RetireGone() {
    InterlockedExchange(&g_ramp.retireReq, 0);
    bool any = false;

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        if (!c || c->state != Rs7CandState::Active || c->gone < 2) {
            continue;
        }

        void* const d3d = c->d3d;
        void* const stock = c->stock;
        void* const ours = c->ours;

        for (auto& b : g_hook.bound) {
            if (b.stock == stock) {
                b = {};
            }
        }

        LogQuiet("ramp: release %s d3d=%p '%s': gone from the engine list", FamilyName(c->family), d3d, c->path);

        if (Rs7Cand_Retire(c, d3d, true)) {
            ReleaseBoth(ours, stock);
            any = true;
        }
    }

    if (any) {
        Rs7Swap_UpdateV6("gone textures released");
    }
}

// Repaints `ours` from its `stock` texture with rgb, exactly as Process built it (same Build + self-check).
// False: why (empty if the stock could not even be described).
bool Repaint(IDirect3DTexture9* stock, IDirect3DTexture9* ours, const uint32_t rgb[2], char* why, size_t whyLen) {
    D3DSURFACE_DESC d{};

    if (FAILED(stock->GetLevelDesc(0, &d))) {
        return false;
    }

    std::vector<Rs7Image> lv(stock->GetLevelCount());

    if (!ReadLevels(stock, uint32_t(d.Format), lv, why, whyLen)) {
        return false;
    }

    const Rs7RampPlan plan =
        Rs7Ramp_Plan({uint32_t(d.Format), d.Width, d.Height, unsigned(lv.size()), uint32_t(d.Pool), d.Usage},
                     g_ramp.cfg.allowInexact);
    CheckedBuild cb;

    return BuildChecked(lv, rgb, plan.exactPossible, cb, why, whyLen) &&
           WriteAndCheck(ours, cb.build.levels, why, whyLen);
}

// Render thread: repaint string 6's lines of every live copy with the requested colours (overlay,
// [color] string6_color / _cb live). Each Active candidate's copy is rebuilt from its stock texture
// exactly as Process built it (same Build + self-check); cached copies no candidate uses are dropped
// (they hold the old colour). A copy that fails is logged and keeps what it had.
void Recolour() {
    InterlockedExchange(&g_ramp.recolourReq, 0);
    const uint32_t rgb[2] = {uint32_t(g_ramp.recolourRgb[0]) & 0xFFFFFF, uint32_t(g_ramp.recolourRgb[1]) & 0xFFFFFF};
    if (rgb[0] == g_ramp.cfg.rgb[0] && rgb[1] == g_ramp.cfg.rgb[1]) {
        return;
    }

    g_ramp.cfg.rgb[0] = rgb[0];
    g_ramp.cfg.rgb[1] = rgb[1];
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

        if (!Repaint(stock, ours, rgb, why, sizeof why)) {
            ++failed;
            LogQuiet("ramp: recolour of copy %p ('%s') failed: %s", static_cast<void*>(ours), c->path, why);
        }

        if (nDone < kRs7MaxCands) {
            done[nDone++] = ours;
        }
    }

    unsigned dropped = 0;

    for (unsigned i = 0; i < g_ramp.copyCount;) {
        bool used = false;

        for (unsigned k = 0; k < nDone; ++k) {
            used = used || done[k] == g_ramp.copies[i].tex;
        }

        if (used) {
            ++i;
            continue;
        }

        g_ramp.copies[i].tex->Release();
        g_ramp.copies[i] = g_ramp.copies[--g_ramp.copyCount];
        ++dropped;
    }

    // RSMods ramps: rebuilt with the new colours at their next bind.
    ResetForeign();
    LogQuiet("ramp: recoloured %u copies to %06X / %06X (%u failed, %u unused cached copies dropped)", nDone - failed,
             unsigned(rgb[0]), unsigned(rgb[1]), failed, dropped);
}
}  // namespace

namespace rs7swap {
IDirect3DBaseTexture9* OnSet(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (stage < kStages) {
        g_hook.bound[stage] = {};
    }
    if (g_ramp.retireReq) {
        RetireGone();
    }
    if (g_ramp.recolourReq && dev == g_hook.dev) {
        Recolour();
    }
    rs7colour::DrainPending();  // review #5: publishes only if this is the manager's (CMBUILD) thread
    if (!tex || !Rs7Cand_Count()) {
        return tex;
    }

    Rs7Cand* c = Rs7Cand_Find(tex);
    if (!c) {
        return OnForeign(dev, stage, tex);
    }

    if (dev != g_hook.dev) {
        if (g_hook.deviceOk) {
            g_hook.deviceOk = false;
            LogQuiet("ramp: candidate d3d=%p bound on device %p, hooked %p: pass-through, swapping off", tex,
                     static_cast<void*>(dev), static_cast<void*>(g_hook.dev));
        }

        Rs7Swap_UpdateV6("SetTexture on another device");
        return tex;
    }

    const Rs7CandState s = c->state;

    if ((s == Rs7CandState::Pending || (s == Rs7CandState::Retry && int32_t(GetTickCount() - c->retryAtMs) >= 0)) &&
        Rs7Cand_Claim(c, tex)) {
        g_ramp.processing = c;
        Process(dev, *c, s);  // render thread, the game is binding it now
        g_ramp.processing = nullptr;
    }

    if (c->state != Rs7CandState::Active || c->d3d != tex) {
        return tex;
    }

    auto* ours = static_cast<IDirect3DBaseTexture9*>(c->ours);

    if (stage < kStages) {
        g_hook.bound[stage] = {tex, ours};
    }
    Rs7Ramp_CountSwap();

    return ours;
}

void AfterFault() {
    Rs7Ramp_CountFault();
    UnlockAfterFault();
    Rs7Cand* c = g_ramp.processing;

    g_ramp.processing = nullptr;
    if (c && c->state == Rs7CandState::Busy) {
        Refuse(*c, "fault while processing");
    }
}

void ResetRamp() {
    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        if (!c || c->state != Rs7CandState::Active) {
            continue;
        }

        ReleaseBoth(c->ours, c->stock);
        c->ours = c->stock = nullptr;
        c->state = Rs7CandState::Refused;
    }

    for (unsigned i = 0; i < g_ramp.copyCount; ++i) {
        g_ramp.copies[i].tex->Release();
    }

    g_ramp.copyCount = 0;
    ResetForeign();
    g_ramp.injectFault = false;
    g_ramp.retireReq = 0;
    g_ramp.recolourReq = 0;
    g_ramp.processing = nullptr;
    ForgetLocked();
}
}  // namespace rs7swap

void Rs7Swap_Configure(const Rs7SwapCfg& c) {
    g_ramp.cfg = c;
    InterlockedExchange(&g_ramp.recolourRgb[0], LONG(c.rgb[0]));
    InterlockedExchange(&g_ramp.recolourRgb[1], LONG(c.rgb[1]));
}

void Rs7Swap_RequestRecolour(const uint32_t rgb[2]) {
    InterlockedExchange(&g_ramp.recolourRgb[0], LONG(rgb[0] & 0xFFFFFF));
    InterlockedExchange(&g_ramp.recolourRgb[1], LONG(rgb[1] & 0xFFFFFF));
    InterlockedExchange(&g_ramp.recolourReq, 1);  // last: the render thread reads the colours after it
}

void Rs7Swap_RequestRetire() {
    InterlockedExchange(&g_ramp.retireReq, 1);
}

void Rs7Swap_SetEngine(CRITICAL_SECTION* listLock, uintptr_t lo, uintptr_t hi) {
    g_ramp.d3d9Lo = lo;
    g_ramp.d3d9Hi = hi;
    g_ramp.listLock = listLock;
}

void Rs7Swap_UpdateV6(const char* reason) {
    Rs7Ramp_UpdateV6(g_hook.installed, g_hook.deviceOk, reason);
}

void Rs7Swap_Stats(Rs7SwapStats* out) {
    Rs7SwapStats st{};

    st.installed = g_hook.installed;
    st.deviceOk = g_hook.deviceOk;
    st.copies = g_ramp.copyCount;
    st.rgb[0] = g_ramp.cfg.rgb[0];
    st.rgb[1] = g_ramp.cfg.rgb[1];

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

void Rs7Swap_InjectFaultForTests(bool on) {
    g_ramp.injectFault = on;
}

void Rs7Swap_SetValidatorForTests(Rs7SwapValidator v) {
    g_ramp.validator = v;
}
