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
namespace rs7swapfix {
using rs7d3d::Device;
using rs7fix::Stock;
inline const Rs7SwapCfg kCfg = {{0xC8C8C8, 0x00FFFF}, false};

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
        Rs7Log_SetSink(Capture);
    }

    ~Capturing() { Rs7Log_SetSink(nullptr); }
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

    Rs7Ramp_LogStats();

    return Lines().empty() ? std::string() : Lines()[0];
}

inline bool Has(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

inline bool V6On() {
    return g_rs7_v6[0] == 0.4453125f && g_rs7_v6[1] == 0.9453125f;
}

inline bool V6Off() {
    return g_rs7_v6[0] == 0.0f && g_rs7_v6[1] == 0.0f;
}

// The engine revalidation needs the game; tests decide the outcome.
inline int g_verdict = 1;

inline int Validate(const Rs7Cand& c, IDirect3DTexture9** out) {
    if (g_verdict != 1) {
        return g_verdict;
    }

    return SUCCEEDED(static_cast<IDirect3DBaseTexture9*>(c.d3d)->QueryInterface(IID_IDirect3DTexture9,
                                                                                reinterpret_cast<void**>(out)))
               ? 1
               : 2;
}

inline void Begin(const Rs7SwapCfg& cfg = kCfg) {
    Rs7Swap_UninstallForTests();
    Rs7Cand_ResetForTests();
    Rs7Swap_Configure(cfg);
    g_verdict = 1;
    Rs7Swap_SetValidatorForTests(Validate);
}

inline void End(IDirect3DDevice9* dev) {
    for (DWORD s = 0; s < 4; ++s) {
        dev->SetTexture(s, nullptr);
    }

    Rs7Swap_UninstallForTests();
    Rs7Cand_ResetForTests();
    Rs7Swap_SetValidatorForTests(nullptr);
}

inline IDirect3DBaseTexture9* RawBound(IDirect3DDevice9* dev, DWORD stage) {
    IDirect3DBaseTexture9* t = nullptr;

    Rs7Swap_RawGetTextureForTests(dev, stage, &t);
    if (t) {
        t->Release();  // the test still holds its own references; compare only
    }

    return t;
}

inline ULONG Refs(IUnknown* u) {
    u->AddRef();

    return u->Release();
}

inline std::vector<Rs7Image> Expected(const std::vector<Rs7Image>& stock, const Rs7SwapCfg& cfg = kCfg) {
    return Rs7Ramp_Build(stock, cfg.rgb).levels;
}

inline void Add(IDirect3DTexture9* t, Rs7Family f = Rs7Family::Note, const char* path = "test/note_gradient_map.dds") {
    Rs7Cand_Add(t, f, path, 0x1000);
}
}  // namespace rs7swapfix
