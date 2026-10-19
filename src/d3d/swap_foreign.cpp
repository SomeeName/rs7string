// RSMods note ramps (render thread only). With custom string colours / Extended Range, RSMods binds its own
// 256x128 ramp on stage 1 after the game bound the stock one (its DrawIndexedPrimitive / DrawPrimitive hooks).
// Its rows 7 / 15 hold fixed greys, so string 6 (V6) would show grey. Each such texture gets its own copy with
// string 6's lines repainted (same Rs7Ramp_Build + self-check as the stock ramps). We hold a reference on the
// source, so its address cannot be reused while it is cached: a rebuilt RSMods texture is a new address.
// Refused textures are cached too (ours = nullptr), so each is read once.
#include "d3d/swap_foreign.h"
#include "d3d/hookutil.h"
#include "d3d/iids.h"
#include "d3d/swap.h"
#include "d3d/swap_hook.h"
#include "d3d/swap_ramp.h"
#include "d3d/swap_texio.h"
#include "features/colours/colours.h"
#include "model/groups.h"
#include <cstdio>
#include <vector>

namespace {
using namespace rs7swap;
using rs7d3d::LogQuiet;
constexpr unsigned kForeignSlots = 8, kForeignMax = 32;

struct Foreign {
    IDirect3DTexture9* src;   // RSMods' texture (our reference)
    IDirect3DTexture9* ours;  // the copy, nullptr = refused (pass-through)
    DWORD lastUse;
};

struct ForeignState {
    Foreign slots[kForeignSlots];
    unsigned seen;  // distinct textures processed since the last recolour
    bool off;       // too many distinct textures (e.g. RSMods rainbow notes): pass-through from then on
} g_foreign;

void ForgetBound(IDirect3DBaseTexture9* ours) {
    for (auto& b : g_hook.bound) {
        if (ours && b.ours == ours) {
            b = {};
        }
    }
}

void ReleaseForeign(Foreign& f) {
    ForgetBound(f.ours);
    if (f.ours) {
        f.ours->Release();
    }
    if (f.src) {
        f.src->Release();
    }
    f = {};
}

void ReleaseAllForeign() {
    for (auto& f : g_foreign.slots) {
        ReleaseForeign(f);
    }
}

// Builds the copy of an RSMods ramp, or nullptr (why). Only ramps whose rows 7 and 15 are grey (RSMods' fixed
// rows for strings 6/7) qualify. RSMods' GDI+ rows are not uniform line by line (in game: non-uniform-rows=16);
// the self-check still requires every line outside the V6 taps to stay as it was.
IDirect3DTexture9* BuildForeign(IDirect3DDevice9* dev, IDirect3DTexture9* t, const D3DSURFACE_DESC& d, char* why,
                                size_t whyLen) {
    const unsigned levels = t->GetLevelCount();
    std::vector<Rs7Image> src(levels);

    if (!ReadLevels(t, uint32_t(d.Format), src, why, whyLen)) {
        return nullptr;
    }

    const Rs7RowStats rs = Rs7Ramp_RowStats(src[0]);

    if (!rs.grey7 || !rs.grey15) {
        std::snprintf(why, whyLen, "not a ramp with grey rows 7/15 (non-uniform-rows=%u grey7=%d grey15=%d)",
                      rs.nonUniformRows, int(rs.grey7), int(rs.grey15));
        return nullptr;
    }

    CheckedBuild cb;

    if (!BuildChecked(src, g_ramp.cfg.rgb, true, cb, why, whyLen)) {
        return nullptr;
    }

    IDirect3DTexture9* ours = nullptr;

    if (MakeCopy(dev, d, levels, d.Format, cb.build.levels, &ours, why, whyLen) != CopyResult::Ok) {
        return nullptr;
    }

    return ours;
}

}  // namespace

namespace rs7swap {
// Render thread, stage 1, not a stock candidate: an RSMods ramp gets its copy bound instead.
IDirect3DBaseTexture9* OnForeign(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (!g_ramp.cfg.rsmods || g_foreign.off || stage != 1 || dev != g_hook.dev || g_rs7_v6[0] == 0.0f) {
        return tex;
    }

    Foreign* hit = nullptr;

    for (auto& f : g_foreign.slots) {
        if (f.src && static_cast<IDirect3DBaseTexture9*>(f.src) == tex) {
            hit = &f;
            break;
        }
    }

    if (!hit) {
        IDirect3DTexture9* t = nullptr;
        if (FAILED(tex->QueryInterface(kRs7IID_Texture9, reinterpret_cast<void**>(&t))) || !t) {
            return tex;
        }

        D3DSURFACE_DESC d{};

        // Cheap filter first (every stage-1 bind passes here): RSMods creates its ramps 256x128 A8R8G8B8 MANAGED
        // (D3D::GenerateTexture / GenerateExactColorTexture); the game's own ramps are X8R8G8B8 or DXT and are
        // handled as candidates only (a stock ramp the worker has not listed yet stays stock).
        if (FAILED(t->GetLevelDesc(0, &d)) || d.Width != 256 || d.Height != 128 || d.Format != D3DFMT_A8R8G8B8 ||
            d.Pool != D3DPOOL_MANAGED || d.Usage) {
            t->Release();
            return tex;
        }

        if (++g_foreign.seen > kForeignMax) {
            t->Release();
            ReleaseAllForeign();
            g_foreign.off = true;
            LogQuiet("ramp: more than %u RSMods note ramps (rainbow notes?): string 6 keeps RSMods' row from now on",
                     kForeignMax);
            return tex;
        }

        char why[160] = {};
        IDirect3DTexture9* ours = BuildForeign(dev, t, d, why, sizeof why);

        hit = &g_foreign.slots[0];

        for (auto& f : g_foreign.slots) {
            if (!f.src) {
                hit = &f;
                break;
            }

            if (f.lastUse < hit->lastUse) {
                hit = &f;
            }
        }

        ReleaseForeign(*hit);
        *hit = {t, ours, 0};  // keeps t's reference

        if (ours) {
            LogQuiet("ramp: RSMods ramp %p (levels=%u) -> copy %p with string 6 %06X / %06X", static_cast<void*>(tex),
                     t->GetLevelCount(), static_cast<void*>(ours), unsigned(g_ramp.cfg.rgb[0]),
                     unsigned(g_ramp.cfg.rgb[1]));
        } else {
            LogQuiet("ramp: RSMods ramp %p (levels=%u) left alone: %s", static_cast<void*>(tex), t->GetLevelCount(),
                     why);
        }
    }

    hit->lastUse = GetTickCount();
    if (!hit->ours) {
        return tex;
    }

    g_hook.bound[stage] = {tex, hit->ours};
    Rs7Ramp_CountSwap();

    return hit->ours;
}

void ResetForeign() {
    ReleaseAllForeign();
    g_foreign.seen = 0;
    g_foreign.off = false;
}
}  // namespace rs7swap
