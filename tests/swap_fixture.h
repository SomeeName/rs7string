#pragma once
#include "d3d/swap.h"
#include "d3d_fixture.h"
#include "features/colours/colours.h"
#include "hook_rules.h"
#include "platform/log.h"
#include "ramp_fixture.h"
#include <windows.h>
#include <cstdint>
#include <d3d9.h>
#include <string>
#include <vector>

// Shared by test_d3d_hook.cpp, test_d3d_ramp.cpp and test_d3d_foreign.cpp: a captured log, the V6 state,
// the swap's setup and teardown on a real device, and the engine revalidation stand-in.
namespace rsxrswapfix {
using rsxrd3d::Device;
using rsxrfix::Stock;
inline const RsxrSwapCfg kCfg = {{0xC8C8C8, 0x00FFFF}};

inline std::vector<std::string>& Lines() {
    static std::vector<std::string> v;

    return v;
}

inline void Capture(const char* msg) {
    Lines().push_back(msg);
}

struct Capturing {
    Capturing() {
        Lines().clear();
        RsxrLog_SetSink(Capture);
    }

    ~Capturing() { RsxrLog_SetSink(nullptr); }
};

inline bool Logged(const char* a, const char* b = "") {
    for (const auto& l : Lines()) {
        if (l.find(a) != std::string::npos && l.find(b) != std::string::npos) {
            return true;
        }
    }

    return false;
}

inline std::string Stats() {
    Capturing cap;

    RsxrRamp_LogStats();

    return Lines().empty() ? std::string() : Lines()[0];
}

inline bool Has(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

inline bool V6On() {
    return g_rsxr_v6[0] == 0.4453125f && g_rsxr_v6[1] == 0.9453125f;
}

inline bool V6Off() {
    return g_rsxr_v6[0] == 0.0f && g_rsxr_v6[1] == 0.0f;
}

// The engine revalidation needs the game; tests decide the outcome.
inline int g_verdict = 1;

inline int Validate(const RsxrCand& c, IDirect3DTexture9** out) {
    if (g_verdict != 1) {
        return g_verdict;
    }

    return SUCCEEDED(static_cast<IDirect3DBaseTexture9*>(c.d3d)->QueryInterface(IID_IDirect3DTexture9,
                                                                                reinterpret_cast<void**>(out)))
               ? 1
               : 2;
}

inline void Begin(const RsxrSwapCfg& cfg = kCfg) {
    RsxrSwap_UninstallForTests();
    RsxrCand_ResetForTests();
    RsxrSwap_Configure(cfg);
    g_verdict = 1;
    RsxrSwap_SetValidatorForTests(Validate);
}

inline void End(IDirect3DDevice9* dev) {
    for (DWORD s = 0; s < 4; ++s) {
        dev->SetTexture(s, nullptr);
    }

    RsxrSwap_UninstallForTests();
    RsxrCand_ResetForTests();
    RsxrSwap_SetValidatorForTests(nullptr);
}

inline IDirect3DBaseTexture9* RawBound(IDirect3DDevice9* dev, DWORD stage) {
    IDirect3DBaseTexture9* t = nullptr;

    RsxrSwap_RawGetTextureForTests(dev, stage, &t);
    if (t) {
        t->Release();  // the test still holds its own references; compare only
    }

    return t;
}

inline ULONG Refs(IUnknown* u) {
    u->AddRef();

    return u->Release();
}

// Like BuildChecked (swap_texio.cpp): every copy also carries the eighth string's V7 band.
inline std::vector<RsxrImage> Expected(const std::vector<RsxrImage>& stock, const RsxrSwapCfg& cfg = kCfg) {
    const uint32_t rgb8[2] = {rsxrcolour::Colour8(0), rsxrcolour::Colour8(1)};

    return RsxrRamp_Build(stock, cfg.rgb, rgb8).levels;
}

inline void Add(IDirect3DTexture9* t, RsxrFamily f = RsxrFamily::Note,
                const char* path = "test/note_gradient_map.dds") {
    RsxrCand_Add(t, f, path, 0x1000);
}
}  // namespace rsxrswapfix
