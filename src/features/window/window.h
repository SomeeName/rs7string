#pragma once
#include <windows.h>

// Test-driving aid (rsxr.ini [window], all off by default):
//   background=0|1   1: the game window never activates itself: ShowWindow gets the no-activate
//                    variant and SetWindowPos gets SWP_NOACTIVATE; SetForegroundWindow,
//                    SetActiveWindow, BringWindowToTop and SetFocus are skipped for the game
//                    window. Only while the game window is not the foreground window: once the
//                    user clicks into it, activation calls are stock. Focus-loss messages are
//                    consumed by a background-only subclass (independent of the overlay).
//                    GetForegroundWindow calls from the exe on the game's window thread report
//                    the game window; library/overlay callers see the real foreground window.
//                    GetFocus on the game's thread reports the game window instead of NULL (the
//                    game's input dispatch needs it), and the game's keyboard is held to
//                    DISCL_FOREGROUND (src/dinput_wrap.h).
//   screen=0|N       N >= 1: centre the game window in the work area of monitor N (1 = the primary
//                    monitor, 2.. = the other monitors in EnumDisplayMonitors order). 0: stock
//                    placement (the game centres on the primary monitor).
// Installed from DllMain, before the game creates its window: inline hooks on user32's
// CreateWindowExA, ShowWindow, MoveWindow, SetWindowPos (+ activation/focus hooks when background=1).
// The game window is the first top-level captioned window it creates with CreateWindowExA.
void RsxrWindow_Install(const wchar_t* iniPath);
// [window] background=1 (valid after RsxrWindow_Install).
bool RsxrWindow_Background();

// Pure helpers (exposed for tests).
// Top-left corner that centres a w x h window in `work`, clamped so the top-left stays inside.
POINT RsxrWindow_Centre(const RECT& work, int w, int h);
// The no-activate variant of a ShowWindow command (SW_HIDE, SW_MINIMIZE etc. unchanged).
int RsxrWindow_NoActivateCmd(int cmd);

// Whether to consume a focus-loss message, and the result to return without calling the next
// procedure. No synthetic activation is sent (DefWindowProc could otherwise change real focus).
struct RsxrWindow_MessageFilter {
    bool consume;
    LRESULT result;
};

RsxrWindow_MessageFilter RsxrWindow_FilterMessage(UINT message, WPARAM w, LPARAM l, bool background);

// rsxr.ini [window] console_quickedit_off=0|1 (default 1).
// The CDLC enabler (D3DX9_42.dll) opens a console with AllocConsole. With QuickEdit on, one mouse
// click into that window starts a selection ("Auswaehlen"/"Select" in the title) and from then on
// every write to the console blocks until the selection ends (Enter / Esc / right click). A game
// thread that writes to the console stalls with it. 1: once the process has a console (polled for
// up to 60 s from startup on its own thread), clear ENABLE_QUICK_EDIT_MODE on its input buffer
// (ENABLE_EXTENDED_FLAGS set so the change is applied). A selection started via the system menu
// (Edit > Mark) still blocks; mouse clicks no longer do. Logged as "console: ...".
void RsxrConsole_Start(const wchar_t* iniPath);

// Pure helper (exposed for tests): the input mode with QuickEdit off, everything else unchanged.
DWORD RsxrConsole_QuickEditOffMode(DWORD mode);

// The [window] keys (overlay and template); RsxrWindow_Install and RsxrConsole_Start read them at startup.
struct RsxrSetTable;
extern const RsxrSetTable kRsxrWindowSettings;
// The Window tab's window section (platform/uilayout.h).
struct RsxrUiTable;
extern const RsxrUiTable kRsxrWindowUi;
