#include "features/frets/frets.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/state.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <intrin.h>

// The debug experiments on a 7-string guitar's headstock: [debug] head_cull keeps one named frame node culled,
// head_stretch / head_stretch_mode stretch the frame about the nut and re-place the pegs. Hot-reloaded from the ini
// every 120 frames until the overlay takes over (Rs7Head_SetLive). Game thread, except Rs7Head_SetLive.

namespace {
using namespace rs7head;
using rs7asset::kChildren;
using rs7asset::kChildSlots;
using rs7asset::kFlags;
using rs7asset::Rd;

constexpr uint32_t kWorldRot = 0x80;
constexpr uint32_t kWorldPos = 0xA4;
constexpr int kMaxNodes = 128;

bool Take(const char* cull, float st, int mode) {
    bool changed = false;
    char narrow[48] = {};

    for (int i = 0; i < 47 && cull[i]; ++i) {
        narrow[i] = char(cull[i] & 0x7F);
    }

    if (std::strcmp(narrow, g_head.cullName) != 0) {
        std::memcpy(g_head.cullName, narrow, sizeof narrow);
        changed = true;
    }

    changed = changed || mode != g_head.stretchMode || st != g_head.stretch;
    g_head.stretchMode = mode;
    g_head.stretch = st;

    return changed;
}

float ReadFloat(const wchar_t* key, float def, float lo, float hi) {
    wchar_t buf[64] = {};

    GetPrivateProfileStringW(L"debug", key, L"", buf, 64, g_head.ini);
    wchar_t* end = nullptr;
    const double v = std::wcstod(buf, &end);
    if (!buf[0] || end == buf || !std::isfinite(v) || v < lo || v > hi) {
        return def;
    }

    return float(v);
}

void Reload() {
    if (g_head.liveOwned) {
        const long seq = g_head.liveSeq;
        if (seq == g_head.liveTaken) {
            return;
        }

        while (_InterlockedCompareExchange(&g_head.liveSpin, 1, 0) != 0) {
            _mm_pause();
        }

        const Live l = g_head.live;

        _InterlockedExchange(&g_head.liveSpin, 0);
        g_head.liveTaken = seq;
        Take(l.cull, l.st, l.mode);
        return;
    }

    if (!g_head.ini[0] || ++g_head.reloadFrames < 120) {
        return;
    }

    g_head.reloadFrames = 0;
    wchar_t cull[48] = {};

    GetPrivateProfileStringW(L"debug", L"head_cull", L"", cull, 48, g_head.ini);
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
    uint8_t* fr = (g_head.cullName[0] && frame) ? rs7s7d::EntityRoot(frame) : nullptr;

    if (fr) {
        uint8_t* nodes[kMaxNodes];
        const int n = Collect(fr, nodes);

        for (int i = 0; i < n && !target; ++i) {
            char name[48] = {};

            rs7s7d::NameOfNode(nodes[i], name, sizeof name);
            if (!std::strcmp(name, g_head.cullName)) {
                target = nodes[i];
            }
        }
    }

    if (g_head.culled && g_head.culled != target) {
        const uint16_t f = uint16_t(Rd<uint16_t>(g_head.culled, kFlags) & ~1u);

        rs7s7d::SafeStore(g_head.culled + kFlags, &f, sizeof f);
    }

    g_head.culled = target;

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

    const float k = g_head.stretch;
    const float* o = reinterpret_cast<const float*>(guitar + 0x2C);
    const float nutY = At<float>(obj0, 0x14 + 4);
    const float m[9] = {1.0f, 0.0f, 0.0f, 0.0f, k, 0.0f, 0.0f, 0.0f, 1.0f};

    rs7s7d::SafeStore(root + 0x4C, m, sizeof m);
    const float t[3] = {o[0], o[1] + (nutY - o[1]) * (1.0f - k), o[2]};

    GameSetVec(g_rs7_fn_setpos, frame, t);
    if (g_head.stretchMode == 1 && g_rs7_fn_setscale) {
        GameSetScaleU(frame, k);
    }
    if (Once(&g_head.logStretch)) {
        Rs7_HeadLog("headstock stretch (experiment): frame root %p, k %p/1000, nut y %p/1000", root,
                    reinterpret_cast<void*>(intptr_t(k * 1000.0f)), reinterpret_cast<void*>(intptr_t(nutY * 1000.0f)));
    }
    g_head.stretchApplied = k;
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

void rs7head::DebugFrame(uint8_t* guitar, uint8_t* head, uint8_t* pegset) {
    Reload();
    if (g_head.cullName[0] || g_head.culled) {
        ApplyCull(At<uint8_t*>(head, kHeadFrame));
    }
    if (g_head.stretch != 1.0f || g_head.stretchApplied != 1.0f) {
        ApplyStretch(guitar, head, pegset);
    }
}

void Rs7Head_SetLive(const char* cull, float stretch, int mode) {
    Live l{};

    for (int i = 0; i < 47 && cull && cull[i]; ++i) {
        l.cull[i] = cull[i];
    }

    l.st = stretch;
    l.mode = mode;

    while (_InterlockedCompareExchange(&g_head.liveSpin, 1, 0) != 0) {
        _mm_pause();
    }

    g_head.live = l;
    _InterlockedExchange(&g_head.liveSpin, 0);
    _InterlockedExchange(&g_head.liveOwned, 1);
    _InterlockedIncrement(&g_head.liveSeq);
}

void Rs7Head_SetIni(const wchar_t* path) {
    g_head.ini[0] = 0;
    if (path && std::wcslen(path) < MAX_PATH) {
        wcscpy_s(g_head.ini, path);
    }
}
