#include "test.h"
#define DIRECTINPUT_VERSION 0x0800
#include "core/dinput.h"
#include <dinput.h>

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
