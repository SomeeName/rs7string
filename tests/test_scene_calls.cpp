#include "executable.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

TEST(scene_show_passes_entity_and_cdecl_visibility) {
    rsxrtest::Restore show(g_rsxr_fn_show);
    // Synthetic callee: record the cdecl argument through EAX, then return without popping it.
    rsxrtest::Executable code{0x8B, 0x4C, 0x24, 0x04, 0x89, 0x08, 0xC3};
    uint32_t entity = 0;

    g_rsxr_fn_show = uintptr_t(code.p);
    rsxrs7d::ShowEntity(reinterpret_cast<uint8_t*>(&entity), 7);
    CHECK_EQ(entity, 7u);
    g_rsxr_fn_show = 0;
    rsxrs7d::ShowEntity(reinterpret_cast<uint8_t*>(&entity), 0);
    CHECK_EQ(entity, 7u);
}

TEST(scene_fade_passes_manager_duration_and_constants_with_callee_cleanup) {
    rsxrtest::Restore manager(g_rsxr_fade_mgr);
    // Synthetic callee: record four stack arguments through EAX and pop sixteen bytes.
    rsxrtest::Executable code{0x8B, 0x54, 0x24, 0x04, 0x89, 0x10, 0x8B, 0x54, 0x24, 0x08, 0x89, 0x50, 0x04, 0x8B, 0x54,
                              0x24, 0x0C, 0x89, 0x50, 0x08, 0x8B, 0x54, 0x24, 0x10, 0x89, 0x50, 0x0C, 0xC2, 0x10, 0x00};
    uint32_t entity[4] = {};
    uint32_t mgr = 0x1234;

    g_rsxr_fade_mgr = uintptr_t(&mgr);
    rsxrs7d::FadeEntity(uintptr_t(code.p), reinterpret_cast<uint8_t*>(entity), 0.5f);
    CHECK_EQ(entity[0], mgr);
    CHECK_EQ(entity[1], 0x3F000000u);
    CHECK_EQ(entity[2], 0u);
    CHECK_EQ(entity[3], 0x3F800000u);
    std::memset(entity, 0, sizeof entity);
    rsxrs7d::FadeEntity(0, reinterpret_cast<uint8_t*>(entity), 0.5f);
    CHECK_EQ(entity[0], 0u);
    rsxrs7d::FadeEntity(uintptr_t(code.p), nullptr, 0.5f);
    mgr = 0;
    rsxrs7d::FadeEntity(uintptr_t(code.p), reinterpret_cast<uint8_t*>(entity), 0.5f);
    CHECK_EQ(entity[0], 0u);
    g_rsxr_fade_mgr = 0;
    rsxrs7d::FadeEntity(uintptr_t(code.p), reinterpret_cast<uint8_t*>(entity), 0.5f);
    CHECK_EQ(entity[0], 0u);
}
