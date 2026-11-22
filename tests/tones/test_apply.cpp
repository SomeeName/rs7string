#include "features/tones/game.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cstdint>

extern "C" uintptr_t g_rsxr_tone_apply, g_rsxr_tone_manager;

namespace {
struct Call {
    uint32_t object = 0, manager = 0, player = 0, kind = 0;
    uint32_t result = 1, calls = 0;
};

Call g_call;

// A stock-ABI stand-in executed through the real call-in/return bridge, including ret 0Ch.
void StockApply(RsxrMid::Context& c) {
    g_call.object = RsxrMid::Dword(c.esi);
    g_call.manager = RsxrMid::Dword(c.esp + 4);
    g_call.player = RsxrMid::Dword(c.esp + 8);
    g_call.kind = RsxrMid::Dword(c.esp + 12);
    ++g_call.calls;
    c.eax = g_call.result;
}

RsxrMid::CallIn g_stockApply{&StockApply, RsxrMid::Mode::Full, 12};
}  // namespace

TEST(tone_apply_passes_the_full_object_and_player_through_the_stock_abi) {
    rsxrtest::Restore<uintptr_t> restoreApply(g_rsxr_tone_apply), restoreManager(g_rsxr_tone_manager);
    uint32_t manager = 0x12345678;

    g_call = {};
    g_rsxr_tone_manager = reinterpret_cast<uintptr_t>(&manager);
    g_rsxr_tone_apply = g_stockApply.Entry();
    CHECK(g_rsxr_tone_apply);
    CHECK(rsxrtones::Apply(0x11112222, 1));
    CHECK_EQ(g_call.object, uint32_t(0x11112222));
    CHECK_EQ(g_call.manager, manager);
    CHECK_EQ(g_call.player, uint32_t(1));
    CHECK_EQ(g_call.kind, uint32_t(3));
    CHECK_EQ(g_call.calls, uint32_t(1));

    g_call.result = 0;
    CHECK(!rsxrtones::Apply(0x33334444, 0));
    CHECK_EQ(g_call.object, uint32_t(0x33334444));
    CHECK_EQ(g_call.player, uint32_t(0));
    CHECK_EQ(g_call.calls, uint32_t(2));
    CHECK(!rsxrtones::Apply(1, 2));
    CHECK(!rsxrtones::Apply(0, 0));
    CHECK_EQ(g_call.calls, uint32_t(2));
}
