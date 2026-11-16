#include "test.h"
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include "dinput_wrap.h"
#include <dinput.h>

namespace {
volatile LONG g_createCalls = 0;

HRESULT WINAPI CreateFixture(void* di, REFGUID, void**, LPUNKNOWN) {
    Rs7Dinput_Wrap(di);  // forwarding must hold no installation lock
    InterlockedIncrement(&g_createCalls);

    return E_FAIL;
}
}  // namespace

TEST(dinput_concurrent_installation_preserves_original_forwarding) {
    static void* vt[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void*>(&CreateFixture)};
    void** object = vt;
    const HANDLE start = rs7test::Must(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEvent");

    struct Work {
        HANDLE start;
        void*** object;
    } work{start, &object};

    HANDLE threads[16];
    const LPTHREAD_START_ROUTINE run = [](LPVOID p) -> DWORD {
        const auto& w = *static_cast<Work*>(p);
        if (WaitForSingleObject(w.start, 10000) != WAIT_OBJECT_0) {
            return 1;
        }

        Rs7Dinput_Wrap(w.object);
        const auto create = reinterpret_cast<HRESULT(WINAPI*)(void*, REFGUID, void**, LPUNKNOWN)>((*w.object)[3]);

        create(w.object, GUID{}, nullptr, nullptr);

        return 0;
    };

    for (HANDLE& t : threads) {
        t = rs7test::Must(CreateThread(nullptr, 0, run, &work, 0, nullptr), "CreateThread");
    }

    SetEvent(start);

    if (WaitForMultipleObjects(16, threads, TRUE, 10000) != WAIT_OBJECT_0) {
        std::printf("FATAL: DirectInput installers did not finish\n");
        std::abort();
    }

    for (HANDLE t : threads) {
        CloseHandle(t);
    }

    CloseHandle(start);
    CHECK_EQ(g_createCalls, 16);
}

TEST(dinput_background_holds_keyboard_and_mouse_to_foreground) {
    const DWORD kb = DI8DEVTYPE_KEYBOARD | (DI8DEVTYPEKEYBOARD_PCENH << 8);

    CHECK_EQ(Rs7Dinput_CoopFlags(kb, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE, true),
             DWORD(DISCL_FOREGROUND | DISCL_NONEXCLUSIVE));
    CHECK_EQ(Rs7Dinput_CoopFlags(DI8DEVTYPE_MOUSE, DISCL_BACKGROUND | DISCL_EXCLUSIVE, true),
             DWORD(DISCL_FOREGROUND | DISCL_EXCLUSIVE));
    CHECK_EQ(Rs7Dinput_CoopFlags(kb, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE, true),
             DWORD(DISCL_FOREGROUND | DISCL_NONEXCLUSIVE));
}

TEST(dinput_leaves_gamepads_and_non_background_mode_stock) {
    CHECK_EQ(Rs7Dinput_CoopFlags(DI8DEVTYPE_GAMEPAD, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE, true),
             DWORD(DISCL_BACKGROUND | DISCL_NONEXCLUSIVE));
    CHECK_EQ(Rs7Dinput_CoopFlags(DI8DEVTYPE_KEYBOARD, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE, false),
             DWORD(DISCL_BACKGROUND | DISCL_NONEXCLUSIVE));
}

TEST(dinput_parse_key_line_accepts_down_and_up) {
    uint8_t dik = 0;
    bool down = false;

    CHECK(Rs7Dinput_ParseKeyLine("k 1E 1", &dik, &down) && dik == 0x1E && down);
    CHECK(Rs7Dinput_ParseKeyLine("k 2c 0", &dik, &down) && dik == 0x2C && !down);
}

TEST(dinput_parse_key_line_rejects_bad_lines) {
    uint8_t dik = 7;
    bool down = true;

    CHECK(!Rs7Dinput_ParseKeyLine("", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 0 1", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 100 1", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 1E 2", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 1E 1 x", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("x 1E 1", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine(nullptr, &dik, &down));
    CHECK(dik == 7 && down);
}

TEST(dinput_parse_key_line_rejects_integer_overflow) {
    uint8_t dik = 7;
    bool down = true;

    CHECK(!Rs7Dinput_ParseKeyLine("k 100000001 1", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 1E 4294967296", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 1E 4294967297", &dik, &down));
    CHECK(!Rs7Dinput_ParseKeyLine("k 100000000000000000000000000000001 1", &dik, &down));
    CHECK(dik == 7 && down);
}

TEST(dinput_mask_mouse_keeps_movement_and_clears_wheel_and_buttons) {
    DIMOUSESTATE2 s{};

    s.lX = 5;
    s.lY = -3;
    s.lZ = 120;
    s.rgbButtons[0] = 0x80;
    s.rgbButtons[7] = 0x80;
    Rs7Dinput_MaskMouseState(&s, sizeof s);
    CHECK(s.lX == 5 && s.lY == -3 && s.lZ == 0);
    CHECK(s.rgbButtons[0] == 0 && s.rgbButtons[7] == 0);

    DIMOUSESTATE v1{};

    v1.rgbButtons[1] = 0x80;
    Rs7Dinput_MaskMouseState(&v1, sizeof v1);
    CHECK(v1.rgbButtons[1] == 0);
    Rs7Dinput_MaskMouseState(nullptr, sizeof v1);
}

TEST(dinput_mouse_click_or_wheel_events_are_wheel_and_buttons_only) {
    CHECK(!Rs7Dinput_IsMouseClickOrWheel(DIMOFS_X));
    CHECK(!Rs7Dinput_IsMouseClickOrWheel(DIMOFS_Y));
    CHECK(Rs7Dinput_IsMouseClickOrWheel(DIMOFS_Z));
    CHECK(Rs7Dinput_IsMouseClickOrWheel(DIMOFS_BUTTON0));
    CHECK(Rs7Dinput_IsMouseClickOrWheel(DIMOFS_BUTTON7));
}
