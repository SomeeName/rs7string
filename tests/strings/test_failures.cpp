#include "features/fretgrid/fretgrid.h"
#include "features/publish/hooks.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/strings/hooks.h"
#include "model/instrument.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

namespace {
uint8_t g_objects[4][0x110];
unsigned g_allocation, g_failAt;

uint8_t* __cdecl Allocate(uint32_t, uint32_t) {
    const unsigned i = g_allocation++;

    return i == g_failAt || i >= 4 ? nullptr : g_objects[i];
}

void AssignProfile(uint8_t* guitar, uint8_t* info, unsigned count) {
    uint8_t holder[8] = {};
    Rs7Mid::Context c = {};

    rs7gs::UncheckedField<uint8_t*>(holder, 4) = guitar;
    info[rs7::kInfoStrings] = uint8_t(count);
    c.ebx = uint32_t(uintptr_t(holder));
    Rs7_Mid_GIP1(c);
}

void CheckStockLoops(uint8_t* gs, uint8_t* info) {
    for (unsigned kind = kGsBind; kind < kGsKinds; ++kind) {
        CHECK_EQ(Rs7Gs_Use(kind, gs, 0, 0), 0);
        CHECK_EQ(info[rs7::kInfoStrings], 6u);
    }
}
}  // namespace

TEST(strings_profile_assignment_respects_extra_allocation) {
    rs7test::Restore restoreAlloc(g_rs7_fn_alloc);
    rs7test::Restore restoreCtor(g_rs7_fn_strctor);
    rs7test::Restore restoreNative(g_rs7_native_strings);
    rs7test::Restore restoreNativeCount(rs7gs::g_strings.native);
    rs7test::Restore restorePorts(g_rs7_publish_ports);

    g_rs7_fn_alloc = uintptr_t(&Allocate);
    g_rs7_fn_strctor = 0;
    g_rs7_native_strings = g_rs7_publish_ports = 1;

    for (unsigned n : {7u, 8u}) {
        for (unsigned fail = 0; fail <= 4; ++fail) {
            uint8_t guitar[0x40] = {}, gs[rs7gs::kGsSize] = {}, info[rs7gs::kInfoSize] = {};

            Rs7Gs_InfoAllocated(info);
            info[rs7::kInfoStrings] = 6;
            rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarInfo) = info;
            rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarStrings) = gs;
            g_allocation = 0;
            g_failAt = fail;
            Rs7Gs_Construct(guitar, gs);
            AssignProfile(guitar, info, n);
            const unsigned want = fail < 4 ? 6u : n;
            const unsigned loaded = fail < 4 ? 6u : 8u;

            CHECK_EQ(info[rs7::kInfoStrings], want);
            CHECK_EQ(info[rs7gs::kInfoActive], want);
            CHECK_EQ(info[rs7gs::kInfoLoaded], loaded);
            if (fail < 4) {
                CheckStockLoops(gs, info);
            } else {
                CHECK_EQ(Rs7Gs_Use(kGsBind, gs, 0, 0), 1);
            }
            CHECK_EQ(Rs7Gs_MeshCount(gs), loaded);
            CHECK_EQ(Rs7Gs_Use(kGsFree, gs, 0, 0), 1);
            CHECK_EQ(Rs7Gs_InfoFreeSize(info), rs7gs::kInfoSize);
        }
    }
}

TEST(strings_registry_exhaustion_caps_later_profile_assignment) {
    rs7test::Restore restoreAlloc(g_rs7_fn_alloc);
    rs7test::Restore restoreCtor(g_rs7_fn_strctor);
    rs7test::Restore restoreNative(g_rs7_native_strings);
    rs7test::Restore restoreNativeCount(rs7gs::g_strings.native);
    uint32_t tokens[rs7gs::kMaxInfos] = {};

    g_rs7_fn_alloc = uintptr_t(&Allocate);
    g_rs7_fn_strctor = 0;
    g_rs7_native_strings = 1;

    for (auto& token : tokens) {
        token = rs7gs::ReserveInfo();
        CHECK(token != 0);
    }

    for (unsigned n : {7u, 8u}) {
        uint8_t guitar[0x40] = {}, gs[rs7gs::kGsSize] = {}, info[rs7gs::kStockInfoSize + 16] = {};
        uint32_t frame[7] = {};
        Rs7Mid::Context c = {};

        std::memset(info + rs7gs::kStockInfoSize, 0xCD, 16);
        c.esp = uint32_t(uintptr_t(frame));
        Rs7_Mid_GIA1Before(c);
        CHECK_EQ(frame[5], rs7gs::kStockInfoSize);
        c.eax = uint32_t(uintptr_t(info));
        Rs7_Mid_GIA1(c);
        CHECK_EQ(Rs7Gs_InfoNative(info), 0);
        info[rs7::kInfoStrings] = 6;
        rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarInfo) = info;
        rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarStrings) = gs;
        g_allocation = 0;
        g_failAt = 4;
        Rs7Gs_Construct(guitar, gs);
        AssignProfile(guitar, info, n);
        CHECK_EQ(info[rs7::kInfoStrings], 6u);
        CheckStockLoops(gs, info);
        CHECK_EQ(Rs7Gs_MeshCount(gs), 6u);
        CHECK_EQ(Rs7Gs_Use(kGsFree, gs, 0, 0), 1);
        CHECK_EQ(Rs7Gs_InfoFreeSize(info), rs7gs::kStockInfoSize);

        for (unsigned i = rs7gs::kStockInfoSize; i < sizeof info; ++i) {
            CHECK_EQ(info[i], 0xCDu);
        }
    }

    for (auto token : tokens) {
        rs7gs::FinishInfo(token, nullptr);
    }
}

TEST(strings_info_capacity_is_reserved_before_enlarging_allocations) {
    uint32_t frames[65][7] = {};
    uint8_t infos[65][rs7gs::kInfoSize] = {};

    for (unsigned i = 0; i < 65; ++i) {
        Rs7Mid::Context c = {};

        c.esp = uint32_t(uintptr_t(frames[i]));
        Rs7_Mid_GIA1Before(c);
        CHECK_EQ(frames[i][5], i < 64 ? rs7gs::kInfoSize : rs7gs::kStockInfoSize);
    }

    for (unsigned i = 0; i < 65; ++i) {
        Rs7Mid::Context c = {};

        c.esp = uint32_t(uintptr_t(frames[i]));
        c.eax = uint32_t(uintptr_t(infos[i]));
        Rs7_Mid_GIA1(c);
        CHECK_EQ(Rs7Gs_InfoNative(infos[i]), i < 64 ? 1 : 0);
        CHECK_EQ(Rs7Gs_InfoFreeSize(infos[i]), i < 64 ? rs7gs::kInfoSize : rs7gs::kStockInfoSize);
    }
}

TEST(strings_failed_info_allocation_releases_its_reservation) {
    for (unsigned i = 0; i < 65; ++i) {
        uint32_t frame[7] = {};
        Rs7Mid::Context c = {};

        c.esp = uint32_t(uintptr_t(frame));
        Rs7_Mid_GIA1Before(c);
        CHECK_EQ(frame[5], rs7gs::kInfoSize);
        Rs7_Mid_GIA1(c);
    }
}

TEST(strings_six_string_bass_does_not_wait_for_unloaded_extras) {
    rs7test::Restore restorePorts(g_rs7_publish_ports);
    rs7test::Restore restoreGrid(g_rs7_native_fretgrid);
    rs7pubgrid::WaitEntry savedWait[8];

    std::memcpy(savedWait, g_rs7_publish_wait, sizeof savedWait);
    Rs7Publish_GridForgetAll();
    uint8_t grid[4] = {};
    uint8_t guitar[0x40] = {}, gs[rs7gs::kGsSize] = {}, info[rs7gs::kInfoSize] = {};
    uint8_t extra[4][0x60] = {};
    uint8_t *main[2] = {extra[0], extra[1]}, *ghost[2] = {extra[2], extra[3]};

    g_rs7_publish_ports = 1;
    g_rs7_native_fretgrid = 1;
    Rs7Gs_InfoAllocated(info);
    info[rs7::kInfoKind] = 2;
    info[rs7::kInfoStrings] = 6;
    rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarInfo) = info;
    rs7gs::UncheckedField<uint8_t*>(guitar, rs7::kGuitarStrings) = gs;
    rs7gs::UncheckedField<uint8_t*>(grid, 0) = guitar;
    rs7gs::InitTail(gs, guitar, info, main, ghost);
    rs7gs::RefreshCounts(info, true);
    CHECK_EQ(Rs7Gs_ExtraWhy(guitar), 0);
    CHECK_EQ(Rs7Publish_GridBuild(grid, guitar), 2);
    CHECK_EQ(Rs7Gs_InfoFreeSize(info), rs7gs::kInfoSize);
    std::memcpy(g_rs7_publish_wait, savedWait, sizeof savedWait);
}
