#include "features/headstock/headstock.h"
#include "features/headstock/hooks.h"
#include "features/scene/scene.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "model/instrument.h"
#include "test.h"
#include <cstring>

namespace {
uint8_t world[0x110] = {};

uint8_t* __cdecl World() {
    return world;
}

struct Guitar {
    alignas(4) uint8_t guitar[0x28] = {}, head[0x10] = {}, frame[0xE0] = {}, root[0x80] = {}, info[0x10] = {};

    Guitar() {
        uint8_t* p = head;

        std::memcpy(guitar + rs7::kGuitarHead, &p, sizeof p);
        p = info;
        std::memcpy(guitar + rs7::kGuitarInfo, &p, sizeof p);
        p = frame;
        std::memcpy(head + 4, &p, sizeof p);
        frame[0xD8] = 1;
        p = root;
        std::memcpy(frame + 0xDC, &p, sizeof p);
        ResetRotation();
    }

    void ResetRotation(float width = 1.0f) {
        const float m[9] = {1, 0, width, 0, 1, width, 0, 0, width};

        std::memcpy(root + 0x4C, m, sizeof m);
    }

    float Width() const {
        float v = 0;

        std::memcpy(&v, root + 0x54, sizeof v);

        return v;
    }
};
}  // namespace

TEST(headstock_width_keeps_all_live_transforms_and_forgets_destroyed_guitars) {
    const uintptr_t saved[] = {g_rs7_fn_world, g_rs7_fn_setpos, g_rs7_fn_setrot, g_rs7_fn_euler};
    Guitar guitars[8];

    world[0x10C] = 1;
    g_rs7_fn_world = reinterpret_cast<uintptr_t>(&World);
    g_rs7_fn_setpos = g_rs7_fn_setrot = g_rs7_fn_euler = 1;  // no native PegSet: no game call

    for (Guitar& g : guitars) {
        Rs7Asset_ForgetGuitar(g.guitar);
        Rs7Spacing_Record(g.info, 0.5f);
    }

    for (int lap = 0; lap < 3; ++lap) {
        for (Guitar& g : guitars) {
            Rs7Head_Width(g.guitar, true);
            CHECK_EQ(g.Width(), 0.5f);
        }
    }

    Guitar& recycled = guitars[0];

    Rs7Asset_ForgetGuitar(recycled.guitar);
    recycled.ResetRotation(2.0f);
    Rs7Head_Width(recycled.guitar, true);
    CHECK_EQ(recycled.Width(), 1.0f);
    Rs7Head_Width(recycled.guitar, false);
    CHECK_EQ(recycled.Width(), 2.0f);

    for (Guitar& g : guitars) {
        Rs7Asset_ForgetGuitar(g.guitar);
        Rs7Spacing_Forget(g.info);
    }

    g_rs7_fn_world = saved[0];
    g_rs7_fn_setpos = saved[1];
    g_rs7_fn_setrot = saved[2];
    g_rs7_fn_euler = saved[3];
}
