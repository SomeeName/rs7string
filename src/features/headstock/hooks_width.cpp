// [neck] spacing_*: the headstock frame narrowed across the strings by the factor the instrument's attach points were
// scaled by (features/spacing), so the frame's string segments (string_size_N, static model geometry at the stock
// pitch) meet the neck strings at the nut. The frame root's rotation (+4Ch) gets its string axis (local z; the attach
// points differ only in z and are centred on z 0, CONFIRMED in the spacing log lines) scaled by k, and the pegs are
// re-placed on their moved locators. Game thread, every N5 frame of an extended instrument.
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegset.h"
#include "features/headstock/state.h"
#include "features/scene/scene.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <cstring>
#include <intrin.h>

namespace {
using namespace rs7head;
using rs7asset::kGuitarInfo;

constexpr uint32_t kRot = 0x4C;

// Never evict a live root's original transform. Destruction returns the entry through ForgetWidth.
Width* Find(const uint8_t* guitar) {
    Width* free = nullptr;

    for (Width& w : g_head.width) {
        if (w.guitar == guitar) {
            return &w;
        }

        if (!free && !w.guitar) {
            free = &w;
        }
    }

    return free;
}

void Pegs(uint8_t* pegset) {
    if (IsNative(pegset)) {
        GamePlacePegs(pegset, Rs7_SafeRead<uint16_t>(pegset, kPegCount));
    }
}
}  // namespace

void Rs7Head_ForgetWidth(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    for (Width& w : g_head.width) {
        if (w.guitar == guitar) {
            w = Width{};
        }
    }
}

void Rs7Head_Width(uint8_t* guitar, bool extended) {
    uint8_t* head = Rs7_SafeRead<uint8_t*>(guitar, kGuitarHead);
    uint8_t* frame = Rs7_SafeRead<uint8_t*>(head, kHeadFrame);
    uint8_t* root = frame ? rs7s7d::EntityRoot(frame) : nullptr;
    const float k = extended ? Rs7Spacing_Factor(Rs7_SafeRead<uint8_t*>(guitar, kGuitarInfo)) : 1.0f;
    Width* w = guitar ? Find(guitar) : nullptr;

    if (!w) {
        return;
    }

    if (w->guitar == guitar && w->root != root) {
        *w = Width{};  // a new frame instance (or none): the old one went with its model
    }
    if (k == 1.0f && w->guitar != guitar) {
        return;  // stock and nothing to restore: the common case
    }
    if (!root || !g_rs7_fn_setpos || !g_rs7_fn_setrot || !g_rs7_fn_euler) {
        return;
    }

    if (w->guitar != guitar || w->root != root) {
        if (!Rs7_SafeCopyZero(w->stock, root + kRot, sizeof w->stock)) {
            return;
        }

        w->guitar = guitar;
        w->root = root;
        w->applied = 1.0f;
    }

    float m[9];

    std::memcpy(m, w->stock, sizeof m);
    m[2] *= k;
    m[5] *= k;
    m[8] *= k;
    Rs7_SafeStore(root + kRot, m, sizeof m);
    Pegs(Rs7_SafeRead<uint8_t*>(head, kHeadPegs));
    if (k != w->applied && Rs7Log_Upto(&g_head.logWidth, 8)) {
        Rs7Log("spacing: guitar %p headstock frame root %p narrowed across the strings by k %.3f (was %.3f)", guitar,
               root, double(k), double(w->applied));
    }
    w->applied = k;
    if (k == 1.0f) {
        *w = Width{};
    }
}
