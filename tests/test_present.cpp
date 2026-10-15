#include "d3d/present.h"
#include "d3d_fixture.h"
#include "log.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <d3d9.h>
#include <string>
#include <vector>

namespace {
using rs7d3d::Device;
int g_presents, g_before, g_after;
HRESULT g_afterHr;
bool g_crash;
IDirect3DDevice9* g_seen;

void OnPresent(IDirect3DDevice9* dev) {
    ++g_presents;
    g_seen = dev;
    if (g_crash) {
        *static_cast<volatile int*>(nullptr) = 1;  // an access violation inside the callback
    }
}

void Before(IDirect3DDevice9*) {
    ++g_before;
}

void After(IDirect3DDevice9*, HRESULT hr) {
    ++g_after;
    g_afterHr = hr;
}

Rs7PresentCallbacks Cb() {
    return Rs7PresentCallbacks{OnPresent, Before, After};
}

void Begin() {
    Rs7Present_UninstallForTests();
    g_presents = g_before = g_after = 0;
    g_afterHr = E_FAIL;
    g_crash = false;
    g_seen = nullptr;
}

D3DPRESENT_PARAMETERS Params() {  // as the fixture creates the device
    D3DPRESENT_PARAMETERS pp{};

    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.hDeviceWindow = rs7d3d::Window();

    return pp;
}
}  // namespace

TEST(present_thunks_are_hotpatchable) {
    const uint8_t want[5] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};

    CHECK(std::memcmp(reinterpret_cast<const void*>(&Rs7_Thunk_Present), want, 5) == 0);
    CHECK(std::memcmp(reinterpret_cast<const void*>(&Rs7_Thunk_Reset), want, 5) == 0);
}

TEST(present_install_patches_slots_16_and_17_and_calls_back_once_per_frame) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    void** vt = *reinterpret_cast<void***>(d.dev);
    void* const o16 = vt[16];
    void* const o17 = vt[17];

    CHECK(Rs7Present_InstallOn(d.dev, Cb()));
    CHECK(vt[16] == reinterpret_cast<void*>(&Rs7_Thunk_Reset));
    CHECK(vt[17] == reinterpret_cast<void*>(&Rs7_Thunk_Present));
    CHECK(Rs7Present_InstallOn(d.dev, Cb()));  // idempotent

    for (int i = 0; i < 3; ++i) {
        d.dev->BeginScene();
        d.dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(10, 20, 30), 1.0f, 0);
        d.dev->EndScene();
        CHECK(SUCCEEDED(d.dev->Present(nullptr, nullptr, nullptr, nullptr)));
    }

    CHECK_EQ(g_presents, 3);
    CHECK(g_seen == d.dev);
    CHECK_EQ(Rs7Present_Frames(), 3ul);
    Rs7Present_UninstallForTests();
    CHECK(vt[16] == o16 && vt[17] == o17);
}

TEST(present_reset_runs_before_and_after_callbacks) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    CHECK(Rs7Present_InstallOn(d.dev, Cb()));
    D3DPRESENT_PARAMETERS pp = Params();
    const HRESULT hr = d.dev->Reset(&pp);

    CHECK(SUCCEEDED(hr));
    CHECK_EQ(g_before, 1);
    CHECK_EQ(g_after, 1);
    CHECK_EQ(g_afterHr, hr);
    CHECK(SUCCEEDED(d.dev->Present(nullptr, nullptr, nullptr, nullptr)));
    CHECK_EQ(g_presents, 1);
    Rs7Present_UninstallForTests();
}

TEST(present_fault_in_the_callback_is_contained_and_sticky) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    CHECK(Rs7Present_InstallOn(d.dev, Cb()));
    g_crash = true;
    CHECK(SUCCEEDED(d.dev->Present(nullptr, nullptr, nullptr, nullptr)));  // the game's call still goes through
    CHECK_EQ(g_presents, 1);
    CHECK_EQ(Rs7Present_Faults(), 1ul);
    g_crash = false;
    CHECK(SUCCEEDED(d.dev->Present(nullptr, nullptr, nullptr, nullptr)));
    CHECK_EQ(g_presents, 1);  // never called again
    D3DPRESENT_PARAMETERS pp = Params();

    CHECK(SUCCEEDED(d.dev->Reset(&pp)));
    CHECK_EQ(g_before, 1);  // still releases (DEFAULT-pool objects would fail the game's Reset)
    CHECK_EQ(g_after, 0);
    Rs7Present_UninstallForTests();
}

TEST(present_recheck_rehooks_only_when_d3d9_restored_the_slots) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Begin();
    void** vt = *reinterpret_cast<void***>(d.dev);
    void* const o16 = vt[16];
    void* const o17 = vt[17];

    CHECK(Rs7Present_InstallOn(d.dev, Cb()));
    DWORD old = 0;

    VirtualProtect(&vt[16], 8, PAGE_READWRITE, &old);
    vt[16] = o16;
    vt[17] = o17;
    Rs7Present_Recheck();
    CHECK(vt[16] == reinterpret_cast<void*>(&Rs7_Thunk_Reset));
    CHECK(vt[17] == reinterpret_cast<void*>(&Rs7_Thunk_Present));
    void* const foreign = reinterpret_cast<void*>(&Params);

    vt[17] = foreign;
    Rs7Present_Recheck();
    CHECK(vt[17] == foreign);  // someone else's hook: left alone
    vt[17] = reinterpret_cast<void*>(&Rs7_Thunk_Present);
    Rs7Present_UninstallForTests();
    CHECK(vt[16] == o16 && vt[17] == o17);
}

TEST(present_second_device_is_refused) {
    Device a, b;
    if (!rs7d3d::Have(a) || !b) {
        return;
    }

    Begin();
    CHECK(Rs7Present_InstallOn(a.dev, Cb()));
    void** vtb = *reinterpret_cast<void***>(b.dev);

    if (vtb != *reinterpret_cast<void***>(a.dev)) {
        CHECK(!Rs7Present_InstallOn(b.dev, Cb()));
        CHECK(vtb[17] != reinterpret_cast<void*>(&Rs7_Thunk_Present));
    }

    Rs7Present_UninstallForTests();
}

TEST(log_tail_returns_the_newest_lines_oldest_first) {
    char out[3][240];
    unsigned got = 0;
    const unsigned long before = Rs7Log_Tail(out, 0, &got);

    CHECK_EQ(got, 0u);
    Rs7Log("tail test %d", 1);
    Rs7Log("tail test %d", 2);
    Rs7Log("tail test %d", 3);
    const unsigned long after = Rs7Log_Tail(out, 2, &got);

    CHECK_EQ(after, before + 3);
    CHECK_EQ(got, 2u);
    CHECK(std::strstr(out[0], "tail test 2") != nullptr);
    CHECK(std::strstr(out[1], "tail test 3\n") != nullptr);
}
