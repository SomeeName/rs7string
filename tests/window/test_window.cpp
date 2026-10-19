#include "features/window/window.h"
#include "test.h"

TEST(window_centre_centres_in_the_work_area) {
    const RECT second{-2560, 0, 0, 1392};
    const POINT p = Rs7Window_Centre(second, 1936, 1119);

    CHECK_EQ(p.x, -2560 + (2560 - 1936) / 2);
    CHECK_EQ(p.y, (1392 - 1119) / 2);
}

TEST(window_centre_clamps_a_too_large_window_to_the_top_left) {
    const RECT small{100, 50, 1380, 770};
    const POINT p = Rs7Window_Centre(small, 1936, 1119);

    CHECK_EQ(p.x, 100);
    CHECK_EQ(p.y, 50);
}

TEST(window_no_activate_cmd_maps_only_activating_commands) {
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOWNORMAL), SW_SHOWNOACTIVATE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOWDEFAULT), SW_SHOWNOACTIVATE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_RESTORE), SW_SHOWNOACTIVATE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOWMAXIMIZED), SW_SHOWNOACTIVATE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOW), SW_SHOWNA);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOWMINIMIZED), SW_SHOWMINNOACTIVE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_HIDE), SW_HIDE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_MINIMIZE), SW_MINIMIZE);
    CHECK_EQ(Rs7Window_NoActivateCmd(SW_SHOWNA), SW_SHOWNA);
}

TEST(window_background_consumes_only_deactivation_messages) {
    struct Case {
        UINT m;
        WPARAM w;
        LPARAM l;
        bool consume;
        LRESULT result;
    };

    const Case cases[] = {
        {WM_ACTIVATEAPP, FALSE, 42, true, 0},
        {WM_ACTIVATEAPP, TRUE, 42, false, 0},
        {WM_ACTIVATE, WA_INACTIVE, 0x1234, true, 0},
        // The high word is the minimized flag, not part of the activation state.
        {WM_ACTIVATE, MAKELONG(WA_INACTIVE, 1), 0, true, 0},
        {WM_ACTIVATE, WA_ACTIVE, 0x1234, false, 0},
        {WM_ACTIVATE, MAKELONG(WA_CLICKACTIVE, 1), 0, false, 0},
        {WM_KILLFOCUS, 0, 0, true, 0},
        {WM_KILLFOCUS, 0x1234, 0, true, 0},
        {WM_NCACTIVATE, FALSE, -1, true, TRUE},
        {WM_NCACTIVATE, TRUE, -1, false, 0},
        {WM_SETFOCUS, 0x1234, 0, false, 0},
        {WM_CLOSE, 0, 0, false, 0},
        {WM_DESTROY, 0, 0, false, 0},
        {WM_NCDESTROY, 0, 0, false, 0},
        {WM_SIZE, SIZE_MINIMIZED, 0, false, 0},
        {WM_KEYDOWN, 'A', 0x001E0001, false, 0},
        {WM_APP + 0x123, 0x1234, -1, false, 0},
    };

    for (const auto& c : cases) {
        const auto filtered = Rs7Window_FilterMessage(c.m, c.w, c.l, true);

        CHECK_EQ(filtered.consume, c.consume);
        CHECK_EQ(filtered.result, c.result);
        // background=0 must pass even the loss messages through untouched.
        const auto stock = Rs7Window_FilterMessage(c.m, c.w, c.l, false);

        CHECK_EQ(stock.consume, false);
        CHECK_EQ(stock.result, 0);
    }
}

TEST(console_quickedit_off_clears_only_quickedit_and_sets_extended_flags) {
    // conhost default 0x1F7: processed, line, echo, window, mouse, insert, QuickEdit, extended
    CHECK_EQ(Rs7Console_QuickEditOffMode(0x1F7), DWORD(0x1B7));
    CHECK_EQ(Rs7Console_QuickEditOffMode(0x1E7), DWORD(0x1A7));
    // without ENABLE_EXTENDED_FLAGS the QuickEdit bit would be ignored: always set it
    CHECK_EQ(Rs7Console_QuickEditOffMode(0x0047), DWORD(0x0087));
    CHECK_EQ(Rs7Console_QuickEditOffMode(0), DWORD(ENABLE_EXTENDED_FLAGS));
    CHECK_EQ(Rs7Console_QuickEditOffMode(0x1B7), DWORD(0x1B7));
}
