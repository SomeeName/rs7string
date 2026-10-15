#pragma once
#include <windows.h>

// rs7string.ini [window] console_quickedit_off=0|1 (default 1).
// The CDLC enabler (D3DX9_42.dll) opens a console with AllocConsole. With QuickEdit on, one mouse
// click into that window starts a selection ("Auswaehlen"/"Select" in the title) and from then on
// every write to the console blocks until the selection ends (Enter / Esc / right click). A game
// thread that writes to the console stalls with it. 1: once the process has a console (polled for
// up to 60 s from startup on an own thread), clear ENABLE_QUICK_EDIT_MODE on its input buffer
// (ENABLE_EXTENDED_FLAGS set so the change is applied). A selection started via the system menu
// (Edit > Mark) still blocks; mouse clicks no longer do. Logged as "console: ...".
void Rs7Console_Start(const wchar_t* iniPath);

// Pure helper (exposed for tests): the input mode with QuickEdit off, everything else unchanged.
DWORD Rs7Console_QuickEditOffMode(DWORD mode);
