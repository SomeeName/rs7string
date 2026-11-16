#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/state.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "platform/safecopy.h"
#include "platform/spinlock.h"
#include <cstring>
#include <cwchar>
#include <intrin.h>

// The debug experiment on a 7-string guitar's headstock: [debug] head_cull keeps one named frame node culled.
// Hot-reloaded from the ini every 120 frames until the overlay takes over (Rs7Head_SetLive). Game thread, except
// Rs7Head_SetLive.

namespace {
using namespace rs7head;
using rs7asset::kChildren;
using rs7asset::kChildSlots;
using rs7asset::kFlags;

constexpr int kMaxNodes = 128;

// The node name to cull, ASCII only (from the overlay's char or the ini's wchar_t text).
template <class Ch>
void Take(const Ch* cull) {
    char narrow[48] = {};

    for (int i = 0; i < 47 && cull[i]; ++i) {
        narrow[i] = char(cull[i] & 0x7F);
    }

    std::memcpy(g_head.cullName, narrow, sizeof narrow);
}

void Reload() {
    if (g_head.liveOwned) {
        const long seq = g_head.liveSeq;
        if (seq == g_head.liveTaken) {
            return;
        }

        Rs7Spin_Lock(&g_head.liveSpin);

        const Live l = g_head.live;

        Rs7Spin_Unlock(&g_head.liveSpin);
        g_head.liveTaken = seq;
        Take(l.cull);
        return;
    }

    if (!g_head.ini[0] || ++g_head.reloadFrames < 120) {
        return;
    }

    g_head.reloadFrames = 0;
    wchar_t cull[48] = {};

    GetPrivateProfileStringW(L"debug", L"head_cull", L"", cull, 48, g_head.ini);
    Take(cull);
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

        const unsigned cnt = Rs7_SafeRead<uint16_t>(p, kChildSlots);
        const uint8_t* ch = Rs7_SafeRead<const uint8_t*>(p, kChildren);

        for (int i = int(cnt < 64 ? cnt : 64) - 1; i >= 0; --i) {
            uint8_t* c = Rs7_SafeRead<uint8_t*>(ch, 4 * uint32_t(i));

            if (c && sp < kMaxNodes) {
                stack[sp++] = c;
            }
        }
    }

    return n;
}

// Diagnostic: keep the named frame node culled, per guitar (DebugFrame runs once per guitar each frame). A guitar's
// entry is dropped without a write when its frame root changes (the old node may be gone with the old root).
Culled& CulledOf(uint8_t* head) {
    for (Culled& c : g_head.culled) {
        if (c.head == head) {
            return c;
        }
    }

    for (Culled& c : g_head.culled) {
        if (!c.head) {
            c.head = head;
            return c;
        }
    }

    Culled& c = g_head.culled[g_head.culledNext++ % 4];  // an old guitar: forget its node

    c = Culled{head, nullptr, nullptr};

    return c;
}

void SetCullFlag(uint8_t* node, bool on) {
    const uint16_t old = Rs7_SafeRead<uint16_t>(node, kFlags);
    const uint16_t f = uint16_t(on ? (old | 1u) : (old & ~1u));

    Rs7_SafeStore(node + kFlags, &f, sizeof f);
}

void ApplyCull(Culled& c, uint8_t* frame) {
    uint8_t* target = nullptr;
    uint8_t* fr = frame ? rs7s7d::EntityRoot(frame) : nullptr;

    if (fr != c.root) {
        c.root = fr;
        c.node = nullptr;
    }

    if (fr && g_head.cullName[0]) {
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

    if (c.node && c.node != target) {
        SetCullFlag(c.node, false);
    }

    c.node = target;

    if (target) {
        SetCullFlag(target, true);
    }
}

}  // namespace

void rs7head::DebugFrame(uint8_t* head) {
    Reload();
    Culled& c = CulledOf(head);

    if (g_head.cullName[0] || c.node) {
        ApplyCull(c, UncheckedField<uint8_t*>(head, kHeadFrame));
    }
}

void Rs7Head_SetLive(const char* cull) {
    Live l{};

    for (int i = 0; i < 47 && cull && cull[i]; ++i) {
        l.cull[i] = cull[i];
    }

    Rs7Spin_Lock(&g_head.liveSpin);

    g_head.live = l;
    Rs7Spin_Unlock(&g_head.liveSpin);
    _InterlockedExchange(&g_head.liveOwned, 1);
    _InterlockedIncrement(&g_head.liveSeq);
}

void Rs7Head_SetIni(const wchar_t* path) {
    g_head.ini[0] = 0;
    if (path && std::wcslen(path) < MAX_PATH) {
        wcscpy_s(g_head.ini, path);
    }
}
