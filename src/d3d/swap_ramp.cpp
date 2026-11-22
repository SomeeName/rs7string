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
#include "platform/seh.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace rsxrswap {
RampState g_ramp;
}  // namespace rsxrswap

namespace {
using rsxrd3d::FamilyName;
using rsxrd3d::LogQuiet;
using namespace rsxrswap;
constexpr unsigned kMaxRetries = 5, kRetryMs = 2000;
enum class Step { Ok, Refuse, Retry };

// A candidate's family and path, copied before its state is stored (the worker may reuse the entry after that).
struct CandName {
    RsxrFamily family;
    char path[sizeof(RsxrCand::path)];
};

CandName NameOf(const RsxrCand& c) {
    CandName n{c.family, {}};

    std::memcpy(n.path, c.path, sizeof n.path - 1);

    return n;
}

// The V6 rule after `what` happened to a candidate: "<family> '<path>' <what>".
void UpdateV6For(const CandName& n, const char* what) {
    char r[160];

    std::snprintf(r, sizeof r, "%s '%s' %s", FamilyName(n.family), n.path, what);
    RsxrSwap_UpdateV6(r);
}

// ---- revalidation: under the game's list lock, re-check the candidate, then take the
// reference while the engine cannot free the texture. No C++ objects here (SEH).
int EngineAcquire(const RsxrCand& c, IDirect3DTexture9** out) {
    CRITICAL_SECTION* cs = g_ramp.listLock;
    if (!cs || !TryEnterCriticalSection(cs)) {
        return -1;  // never wait on the render thread
    }

    int r = 0;

    __try {
        if (RsxrTex_Revalidate(g_hook.mem, c.obj, reinterpret_cast<uintptr_t>(c.d3d), c.family, g_ramp.d3d9Lo,
                               g_ramp.d3d9Hi)) {
            auto* base = static_cast<IDirect3DBaseTexture9*>(c.d3d);

            r = SUCCEEDED(base->QueryInterface(kRsxrIID_Texture9, reinterpret_cast<void**>(out))) && *out ? 1 : 2;
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        r = 0;
    }

    LeaveCriticalSection(cs);

    return r;
}

// ---- terminal states: set the state last, then apply the V6 rule (both after stock/ours are final).
void Refuse(RsxrCand& c, const char* why) {
    const CandName name = NameOf(c);

    LogQuiet("ramp: refuse %s d3d=%p '%s': %s", FamilyName(name.family), c.d3d, name.path, why);
    c.state = RsxrCandState::Refused;
    UpdateV6For(name, "refused");
}

void RetryLater(RsxrCand& c, const char* why) {
    if (++c.retries >= kMaxRetries) {
        char w[160];

        std::snprintf(w, sizeof w, "%s (%u tries)", why, c.retries);
        Refuse(c, w);
        return;
    }

    const CandName name = NameOf(c);

    LogQuiet("ramp: retry %s d3d=%p '%s' in %u ms (%u/%u): %s", FamilyName(name.family), c.d3d, name.path, kRetryMs,
             c.retries, kMaxRetries, why);
    c.retryAtMs = GetTickCount() + kRetryMs;
    c.state = RsxrCandState::Retry;
    UpdateV6For(name, "retrying");
}

// ---- the copy cache: identical stock ramps (same hash and format) share one copy.
// A new reference on the cached copy, or nullptr.
IDirect3DTexture9* SharedCopy(uint64_t hash, uint32_t fmt) {
    for (unsigned i = 0; i < g_ramp.copyCount; ++i) {
        if (g_ramp.copies[i].hash == hash && g_ramp.copies[i].fmt == fmt &&
            g_ramp.copies[i].colourGeneration == g_ramp.colourGeneration) {
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
    RsxrRampPlan plan;
};

Step BuildNew(IDirect3DDevice9* dev, const Plan& p, const std::vector<RsxrImage>& stock, uint64_t hash,
              IDirect3DTexture9** ours, char* why, size_t whyLen) {
    CheckedBuild cb;

    if (!BuildChecked(stock, g_ramp.cfg.rgb, cb, why, whyLen)) {
        return Step::Refuse;
    }

    switch (MakeCopy(dev, p.d, p.levels, cb.build.levels, ours, why, whyLen)) {
        case CopyResult::CreateFailed:
            return Step::Retry;
        case CopyResult::WriteFailed:
            return Step::Refuse;
        default:
            break;
    }

    LogQuiet("ramp: built %p (hash %016llX): %ux%u fmt=%u levels=%u, custom colour on levels 0-%u, "
             "self-check exact=%d v6-pure=%d changed-lines=%u",
             static_cast<void*>(*ours), static_cast<unsigned long long>(hash), p.d.Width, p.d.Height,
             unsigned(p.d.Format), p.levels, cb.build.customLevels ? cb.build.customLevels - 1 : 0, int(cb.chk.exact),
             int(cb.chk.v6Pure), cb.chk.changedLines);
    if (p.levels > cb.build.customLevels) {
        LogQuiet("ramp: note: levels %u-%u show string 6 as a row 6/7 blend (distance)", cb.build.customLevels,
                 p.levels - 1);
    }

    if (g_ramp.copyCount < kRsxrMaxCands) {
        (*ours)->AddRef();  // the copies table keeps its own reference (sharing after a retire)
        g_ramp.copies[g_ramp.copyCount++] = {hash, uint32_t(p.d.Format), *ours, g_ramp.colourGeneration};
    }

    return Step::Ok;
}

Step PrepareCopy(IDirect3DDevice9* dev, const RsxrCand& c, IDirect3DTexture9* t, IDirect3DTexture9** ours,
                 uint64_t* hash, char* why, size_t whyLen) {
    Plan p{};

    if (FAILED(t->GetLevelDesc(0, &p.d))) {
        std::snprintf(why, whyLen, "GetLevelDesc(0) failed");
        return Step::Refuse;
    }

    const D3DSURFACE_DESC& d = p.d;

    p.levels = t->GetLevelCount();
    p.plan = RsxrRamp_Plan({uint32_t(d.Format), d.Width, d.Height, p.levels, uint32_t(d.Pool), d.Usage});

    if (p.plan.verdict == RsxrRampVerdict::Refuse) {
        std::snprintf(why, whyLen, "%s (fmt=%u %ux%u levels=%u pool=%u usage=%08lX)", p.plan.why, unsigned(d.Format),
                      d.Width, d.Height, p.levels, unsigned(d.Pool), d.Usage);
        return Step::Refuse;
    }

    std::vector<RsxrImage> stock(p.levels);

    if (!ReadLevels(t, stock, why, whyLen)) {
        return Step::Refuse;
    }

    *hash = RsxrRamp_Hash(stock);
    const RsxrRowStats rs = RsxrRamp_RowStats(stock[0]);

    if (c.family == RsxrFamily::Embedded && (rs.nonUniformRows || !rs.grey7 || !rs.grey15)) {
        std::snprintf(why, whyLen, "not a colour ramp (non-uniform-rows=%u grey7=%d grey15=%d)", rs.nonUniformRows,
                      int(rs.grey7), int(rs.grey15));
        return Step::Refuse;
    }

    LogQuiet("ramp: process %s d3d=%p '%s': fmt=%u %ux%u levels=%u pool=%u usage=%08lX hash=%016llX "
             "non-uniform-rows=%u grey7=%d grey15=%d",
             FamilyName(c.family), c.d3d, c.path, unsigned(d.Format), d.Width, d.Height, p.levels, unsigned(d.Pool),
             d.Usage, static_cast<unsigned long long>(*hash), rs.nonUniformRows, int(rs.grey7), int(rs.grey15));

    *ours = SharedCopy(*hash, uint32_t(d.Format));

    if (*ours) {
        LogQuiet("ramp: shared copy %p (hash %016llX) for %s d3d=%p", static_cast<void*>(*ours),
                 static_cast<unsigned long long>(*hash), FamilyName(c.family), c.d3d);
        return Step::Ok;
    }

    return BuildNew(dev, p, stock, *hash, ours, why, whyLen);
}

// Render thread; c is Busy (claimed). Ends in Active, Refused, Retry, Retired or back in `prev`.
void Process(IDirect3DDevice9* dev, RsxrCand& c, RsxrCandState prev) {
    if (g_ramp.injectFault) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);  // a simulated fault (tests)
    }
    IDirect3DTexture9* t = nullptr;

    const int v = g_ramp.validator ? g_ramp.validator(c, &t) : EngineAcquire(c, &t);

    if (v < 0) {  // the list lock is busy: try again at the next bind, pass-through meanwhile
        c.state = prev;
        return;
    }

    if (v == 0) {
        LogQuiet("ramp: drop stale %s d3d=%p '%s': no longer that engine texture", FamilyName(c.family), c.d3d, c.path);
        RsxrCand_Retire(&c, c.d3d, true);
        RsxrSwap_UpdateV6("stale candidate dropped");
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
    c.colourGeneration = g_ramp.colourGeneration;
    c.retries = 0;
    const CandName name = NameOf(c);
    void* const d3d = c.d3d;

    c.state = RsxrCandState::Active;  // last: stock/ours are complete
    LogQuiet("ramp: active %s d3d=%p '%s' -> copy %p", FamilyName(name.family), d3d, name.path,
             static_cast<void*>(ours));
    UpdateV6For(name, "active");
}

// Render thread: release Active entries the worker flagged gone (the engine dropped their texture).
void RetireGone() {
    InterlockedExchange(&g_ramp.retireReq, 0);
    bool any = false;

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        RsxrCand* c = RsxrCand_At(i);
        if (!c || c->state != RsxrCandState::Active || c->gone < 2) {
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

        if (RsxrCand_Retire(c, d3d, true)) {
            ReleaseBoth(ours, stock);
            any = true;
        }
    }

    if (any) {
        RsxrSwap_UpdateV6("gone textures released");
    }
}

// Repaints `ours` from its `stock` texture with rgb, exactly as Process built it (same Build + self-check).
// False: why (empty if the stock could not even be described).
bool Repaint(IDirect3DTexture9* stock, IDirect3DTexture9* ours, const uint32_t rgb[2], char* why, size_t whyLen) {
    D3DSURFACE_DESC d{};

    if (FAILED(stock->GetLevelDesc(0, &d))) {
        return false;
    }

    std::vector<RsxrImage> lv(stock->GetLevelCount());

    if (!ReadLevels(stock, lv, why, whyLen)) {
        return false;
    }

    CheckedBuild cb;

    return BuildChecked(lv, rgb, cb, why, whyLen) && WriteAndCheck(ours, cb.build.levels, why, whyLen);
}

// Publish a generation only after every level was written and checked. Shared candidates and the cache
// must agree: a failed copy cannot be handed to another candidate as if it had the requested colours.
bool RecolourCopy(RsxrCand& c, const uint32_t rgb[2]) {
    if (c.colourGeneration == g_ramp.colourGeneration) {
        return true;
    }

    auto* ours = static_cast<IDirect3DTexture9*>(c.ours);
    char why[160] = {};

    if (!Repaint(static_cast<IDirect3DTexture9*>(c.stock), ours, rgb, why, sizeof why)) {
        LogQuiet("ramp: recolour of copy %p ('%s') failed: %s", static_cast<void*>(ours), c.path, why);
        return false;
    }

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        RsxrCand* shared = RsxrCand_At(i);

        if (shared && shared->state == RsxrCandState::Active && shared->ours == ours) {
            shared->colourGeneration = g_ramp.colourGeneration;
        }
    }

    for (unsigned i = 0; i < g_ramp.copyCount; ++i) {
        if (g_ramp.copies[i].tex == ours) {
            g_ramp.copies[i].colourGeneration = g_ramp.colourGeneration;
        }
    }

    return true;
}

// Render thread: repaint string 6's lines of every live copy with the requested colours (overlay,
// [color] string6_color / _cb live). Each Active candidate's copy is rebuilt from its stock texture
// exactly as Process built it (same Build + self-check); cached copies no candidate uses are dropped
// (they hold the old colour). Failed copies keep their old generation and are retried.
void Recolour() {
    InterlockedExchange(&g_ramp.recolourReq, 0);
    const uint32_t rgb[2] = {uint32_t(g_ramp.recolourRgb[0]) & 0xFFFFFF, uint32_t(g_ramp.recolourRgb[1]) & 0xFFFFFF};
    const uint32_t rgb8[2] = {rsxrcolour::Colour8(0), rsxrcolour::Colour8(1)};  // BuildChecked paints the V7 band
    const bool changed = rgb[0] != g_ramp.cfg.rgb[0] || rgb[1] != g_ramp.cfg.rgb[1] || rgb8[0] != g_ramp.rgb8[0] ||
                         rgb8[1] != g_ramp.rgb8[1];
    if (!changed && !g_ramp.recolourFailed) {
        return;
    }

    if (changed) {
        g_ramp.cfg.rgb[0] = rgb[0];
        g_ramp.cfg.rgb[1] = rgb[1];
        g_ramp.rgb8[0] = rgb8[0];
        g_ramp.rgb8[1] = rgb8[1];
        ++g_ramp.colourGeneration;
        ResetForeign();  // RSMods ramps rebuild with the new colours at their next bind
    }

    IDirect3DTexture9* done[kRsxrMaxCands] = {};
    unsigned nDone = 0, failed = 0;

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        RsxrCand* c = RsxrCand_At(i);
        if (!c || c->state != RsxrCandState::Active || !c->stock || !c->ours) {
            continue;
        }

        auto* ours = static_cast<IDirect3DTexture9*>(c->ours);
        bool seen = false;

        for (unsigned k = 0; k < nDone; ++k) {
            seen = seen || done[k] == ours;
        }

        if (seen) {
            continue;
        }

        if (!RecolourCopy(*c, rgb)) {
            ++failed;
        }

        if (nDone < kRsxrMaxCands) {
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

    g_ramp.recolourFailed = failed != 0;
    g_ramp.recolourRetryAtMs = GetTickCount() + kRetryMs;
    LogQuiet("ramp: recoloured %u copies to %06X / %06X (%u failed, %u unused cached copies dropped)", nDone - failed,
             unsigned(rgb[0]), unsigned(rgb[1]), failed, dropped);
}
}  // namespace

namespace rsxrswap {
IDirect3DBaseTexture9* OnSet(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (dev == g_hook.dev && g_hook.overlayDrawing) {
        return tex;
    }

    if (stage < kStages && dev == g_hook.dev) {
        g_hook.bound[stage] = {};
    }
    if (g_ramp.retireReq) {
        RetireGone();
    }
    if (dev == g_hook.dev &&
        (g_ramp.recolourReq || (g_ramp.recolourFailed && int32_t(GetTickCount() - g_ramp.recolourRetryAtMs) >= 0))) {
        Recolour();
    }
    rsxrcolour::DrainPending();  // publishes only if this is the manager's (CMBUILD) thread
    // Stages >= kStages (the vertex samplers) are not tracked in bound[]: never swapped there.
    if (!tex || stage >= kStages || !RsxrCand_Count()) {
        return tex;
    }

    RsxrCand* c = RsxrCand_Find(tex);
    if (!c) {
        return OnForeign(dev, stage, tex);
    }

    if (dev != g_hook.dev) {
        if (g_hook.deviceOk) {
            g_hook.deviceOk = false;
            LogQuiet("ramp: candidate d3d=%p bound on device %p, hooked %p: pass-through, swapping off", tex,
                     static_cast<void*>(dev), static_cast<void*>(g_hook.dev));
        }

        RsxrSwap_UpdateV6("SetTexture on another device");
        return tex;
    }

    const RsxrCandState s = c->state;

    if ((s == RsxrCandState::Pending || (s == RsxrCandState::Retry && int32_t(GetTickCount() - c->retryAtMs) >= 0)) &&
        RsxrCand_Claim(c, tex)) {
        g_ramp.processing = c;
        Process(dev, *c, s);  // render thread, the game is binding it now
        g_ramp.processing = nullptr;
    }

    if (c->state != RsxrCandState::Active || c->d3d != tex) {
        return tex;
    }

    auto* ours = static_cast<IDirect3DBaseTexture9*>(c->ours);

    if (stage < kStages) {  // always true here; kept for /analyze
        g_hook.bound[stage] = {tex, ours};
    }
    RsxrRamp_CountSwap();

    return ours;
}

void AfterFault() {
    RsxrRamp_CountFault();
    UnlockAfterFault();
    RsxrCand* c = g_ramp.processing;

    g_ramp.processing = nullptr;
    if (c && c->state == RsxrCandState::Busy) {
        Refuse(*c, "fault while processing");
    }
}

void ResetRamp() {
    for (int i = 0; i < RsxrCand_Count(); ++i) {
        RsxrCand* c = RsxrCand_At(i);
        if (!c || c->state != RsxrCandState::Active) {
            continue;
        }

        ReleaseBoth(c->ours, c->stock);
        c->ours = c->stock = nullptr;
        c->state = RsxrCandState::Refused;
    }

    for (unsigned i = 0; i < g_ramp.copyCount; ++i) {
        g_ramp.copies[i].tex->Release();
    }

    g_ramp.copyCount = 0;
    g_ramp.colourGeneration = 0;
    g_ramp.recolourFailed = false;
    g_ramp.rgb8[0] = g_ramp.rgb8[1] = 0xFFFFFFFF;
    ResetForeign();
    g_ramp.injectFault = false;
    g_ramp.retireReq = 0;
    g_ramp.recolourReq = 0;
    g_ramp.processing = nullptr;
    ForgetLocked();
}
}  // namespace rsxrswap

void RsxrSwap_Configure(const RsxrSwapCfg& c) {
    g_ramp.cfg = c;
    InterlockedExchange(&g_ramp.recolourRgb[0], LONG(c.rgb[0]));
    InterlockedExchange(&g_ramp.recolourRgb[1], LONG(c.rgb[1]));
}

void RsxrSwap_RequestRecolour(const uint32_t rgb[2]) {
    InterlockedExchange(&g_ramp.recolourRgb[0], LONG(rgb[0] & 0xFFFFFF));
    InterlockedExchange(&g_ramp.recolourRgb[1], LONG(rgb[1] & 0xFFFFFF));
    InterlockedExchange(&g_ramp.recolourReq, 1);  // last: the render thread reads the colours after it
}

void RsxrSwap_RequestRetire() {
    InterlockedExchange(&g_ramp.retireReq, 1);
}

void RsxrSwap_SetEngine(CRITICAL_SECTION* listLock, uintptr_t lo, uintptr_t hi) {
    g_ramp.d3d9Lo = lo;
    g_ramp.d3d9Hi = hi;
    g_ramp.listLock = listLock;
}

void RsxrSwap_UpdateV6(const char* reason) {
    RsxrRamp_UpdateV6(g_hook.installed, g_hook.deviceOk, reason);
}

void RsxrSwap_Stats(RsxrSwapStats* out) {
    RsxrSwapStats st{};

    st.installed = g_hook.installed;
    st.deviceOk = g_hook.deviceOk;
    st.copies = g_ramp.copyCount;
    st.rgb[0] = g_ramp.cfg.rgb[0];
    st.rgb[1] = g_ramp.cfg.rgb[1];

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        const RsxrCand* c = RsxrCand_At(i);
        if (!c || !c->d3d) {
            continue;
        }

        switch (c->state) {
            case RsxrCandState::Active:
                ++st.active;
                break;
            case RsxrCandState::Refused:
                ++st.refused;
                break;
            case RsxrCandState::Retry:
                ++st.retry;
                break;
            default:
                ++st.pending;
                break;
        }
    }

    *out = st;
}

void RsxrSwap_InjectFaultForTests(bool on) {
    g_ramp.injectFault = on;
}

void RsxrSwap_SetValidatorForTests(RsxrSwapValidator v) {
    g_ramp.validator = v;
}
