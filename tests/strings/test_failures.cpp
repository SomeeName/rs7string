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
    RsxrMid::Context c = {};

    rsxrgs::UncheckedField<uint8_t*>(holder, 4) = guitar;
    info[rsxr::kInfoStrings] = uint8_t(count);
    c.ebx = uint32_t(uintptr_t(holder));
    Rsxr_Mid_GIP1(c);
}

void CheckStockLoops(uint8_t* gs, uint8_t* info) {
    for (unsigned kind = kGsBind; kind < kGsKinds; ++kind) {
        CHECK_EQ(RsxrGs_Use(kind, gs, 0, 0), 0);
        CHECK_EQ(info[rsxr::kInfoStrings], 6u);
    }
}
}  // namespace

TEST(strings_profile_assignment_respects_extra_allocation) {
    rsxrtest::Restore restoreAlloc(g_rsxr_fn_alloc);
    rsxrtest::Restore restoreCtor(g_rsxr_fn_strctor);
    rsxrtest::Restore restoreNative(g_rsxr_native_strings);
    rsxrtest::Restore restoreNativeCount(rsxrgs::g_strings.native);
    rsxrtest::Restore restorePorts(g_rsxr_publish_ports);

    g_rsxr_fn_alloc = uintptr_t(&Allocate);
    g_rsxr_fn_strctor = 0;
    g_rsxr_native_strings = g_rsxr_publish_ports = 1;

    for (unsigned n : {7u, 8u}) {
        for (unsigned fail = 0; fail <= 4; ++fail) {
            uint8_t guitar[0x40] = {}, gs[rsxrgs::kGsSize] = {}, info[rsxrgs::kInfoSize] = {};

            RsxrGs_InfoAllocated(info);
            info[rsxr::kInfoStrings] = 6;
            rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarInfo) = info;
            rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarStrings) = gs;
            g_allocation = 0;
            g_failAt = fail;
            RsxrGs_Construct(guitar, gs);
            AssignProfile(guitar, info, n);
            const unsigned want = fail < 4 ? 6u : n;
            const unsigned loaded = fail < 4 ? 6u : 8u;

            CHECK_EQ(info[rsxr::kInfoStrings], want);
            CHECK_EQ(info[rsxrgs::kInfoActive], want);
            CHECK_EQ(info[rsxrgs::kInfoLoaded], loaded);
            if (fail < 4) {
                CheckStockLoops(gs, info);
            } else {
                CHECK_EQ(RsxrGs_Use(kGsBind, gs, 0, 0), 1);
            }
            CHECK_EQ(RsxrGs_MeshCount(gs), loaded);
            CHECK_EQ(RsxrGs_Use(kGsFree, gs, 0, 0), 1);
            CHECK_EQ(RsxrGs_InfoFreeSize(info), rsxrgs::kInfoSize);
        }
    }
}

TEST(strings_registry_exhaustion_caps_later_profile_assignment) {
    rsxrtest::Restore restoreAlloc(g_rsxr_fn_alloc);
    rsxrtest::Restore restoreCtor(g_rsxr_fn_strctor);
    rsxrtest::Restore restoreNative(g_rsxr_native_strings);
    rsxrtest::Restore restoreNativeCount(rsxrgs::g_strings.native);
    uint32_t tokens[rsxrgs::kMaxInfos] = {};

    g_rsxr_fn_alloc = uintptr_t(&Allocate);
    g_rsxr_fn_strctor = 0;
    g_rsxr_native_strings = 1;

    for (auto& token : tokens) {
        token = rsxrgs::ReserveInfo();
        CHECK(token != 0);
    }

    for (unsigned n : {7u, 8u}) {
        uint8_t guitar[0x40] = {}, gs[rsxrgs::kGsSize] = {}, info[rsxrgs::kStockInfoSize + 16] = {};
        uint32_t frame[7] = {};
        RsxrMid::Context c = {};

        std::memset(info + rsxrgs::kStockInfoSize, 0xCD, 16);
        c.esp = uint32_t(uintptr_t(frame));
        Rsxr_Mid_GIA1Before(c);
        CHECK_EQ(frame[5], rsxrgs::kStockInfoSize);
        c.eax = uint32_t(uintptr_t(info));
        Rsxr_Mid_GIA1(c);
        CHECK_EQ(RsxrGs_InfoNative(info), 0);
        info[rsxr::kInfoStrings] = 6;
        rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarInfo) = info;
        rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarStrings) = gs;
        g_allocation = 0;
        g_failAt = 4;
        RsxrGs_Construct(guitar, gs);
        AssignProfile(guitar, info, n);
        CHECK_EQ(info[rsxr::kInfoStrings], 6u);
        CheckStockLoops(gs, info);
        CHECK_EQ(RsxrGs_MeshCount(gs), 6u);
        CHECK_EQ(RsxrGs_Use(kGsFree, gs, 0, 0), 1);
        CHECK_EQ(RsxrGs_InfoFreeSize(info), rsxrgs::kStockInfoSize);

        for (unsigned i = rsxrgs::kStockInfoSize; i < sizeof info; ++i) {
            CHECK_EQ(info[i], 0xCDu);
        }
    }

    for (auto token : tokens) {
        rsxrgs::FinishInfo(token, nullptr);
    }
}

TEST(strings_info_capacity_is_reserved_before_enlarging_allocations) {
    uint32_t frames[65][7] = {};
    uint8_t infos[65][rsxrgs::kInfoSize] = {};

    for (unsigned i = 0; i < 65; ++i) {
        RsxrMid::Context c = {};

        c.esp = uint32_t(uintptr_t(frames[i]));
        Rsxr_Mid_GIA1Before(c);
        CHECK_EQ(frames[i][5], i < 64 ? rsxrgs::kInfoSize : rsxrgs::kStockInfoSize);
    }

    for (unsigned i = 0; i < 65; ++i) {
        RsxrMid::Context c = {};

        c.esp = uint32_t(uintptr_t(frames[i]));
        c.eax = uint32_t(uintptr_t(infos[i]));
        Rsxr_Mid_GIA1(c);
        CHECK_EQ(RsxrGs_InfoNative(infos[i]), i < 64 ? 1 : 0);
        CHECK_EQ(RsxrGs_InfoFreeSize(infos[i]), i < 64 ? rsxrgs::kInfoSize : rsxrgs::kStockInfoSize);
    }
}

TEST(strings_failed_info_allocation_releases_its_reservation) {
    for (unsigned i = 0; i < 65; ++i) {
        uint32_t frame[7] = {};
        RsxrMid::Context c = {};

        c.esp = uint32_t(uintptr_t(frame));
        Rsxr_Mid_GIA1Before(c);
        CHECK_EQ(frame[5], rsxrgs::kInfoSize);
        Rsxr_Mid_GIA1(c);
    }
}

TEST(strings_six_string_bass_does_not_wait_for_unloaded_extras) {
    rsxrtest::Restore restorePorts(g_rsxr_publish_ports);
    rsxrtest::Restore restoreGrid(g_rsxr_native_fretgrid);
    rsxrpubgrid::WaitEntry savedWait[8];

    std::memcpy(savedWait, g_rsxr_publish_wait, sizeof savedWait);
    RsxrPublish_GridForgetAll();
    uint8_t grid[4] = {};
    uint8_t guitar[0x40] = {}, gs[rsxrgs::kGsSize] = {}, info[rsxrgs::kInfoSize] = {};
    uint8_t extra[4][0x60] = {};
    uint8_t *main[2] = {extra[0], extra[1]}, *ghost[2] = {extra[2], extra[3]};

    g_rsxr_publish_ports = 1;
    g_rsxr_native_fretgrid = 1;
    RsxrGs_InfoAllocated(info);
    info[rsxr::kInfoKind] = 2;
    info[rsxr::kInfoStrings] = 6;
    rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarInfo) = info;
    rsxrgs::UncheckedField<uint8_t*>(guitar, rsxr::kGuitarStrings) = gs;
    rsxrgs::UncheckedField<uint8_t*>(grid, 0) = guitar;
    rsxrgs::InitTail(gs, guitar, info, main, ghost);
    rsxrgs::RefreshCounts(info, true);
    CHECK_EQ(RsxrGs_ExtraWhy(guitar), 0);
    CHECK_EQ(RsxrPublish_GridBuild(grid, guitar), 2);
    CHECK_EQ(RsxrGs_InfoFreeSize(info), rsxrgs::kInfoSize);
    std::memcpy(g_rsxr_publish_wait, savedWait, sizeof savedWait);
}
