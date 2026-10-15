#pragma once
#include <windows.h>
#include <cstdint>

// The game's DirectInput object (from our DirectInput8Create proxy). With [window] background=1
// the keyboard and mouse are held to DISCL_FOREGROUND: the game window is never the foreground
// window while test-driven, so the owner's real keyboard/mouse input in other windows can never
// reach the game (needed because GetFocus reports the game window, see core/window.h). Logs each
// device's cooperative level either way. Patches the IDirectInput8 / IDirectInputDevice8 vtables.
void Rs7Dinput_Wrap(void* directInput8);

// Background test driving: the game's keyboard device also returns keys injected through the
// named pipe "rs7string" (lines "k <DIK hex> <1|0>", scripts/rsdrive.py), and reports itself
// acquired while the real keyboard is not (the window is never foreground).

// One pipe line -> DIK code and down/up; false for anything else (exposed for tests).
bool Rs7Dinput_ParseKeyLine(const char* line, uint8_t* dik, bool* down);

// The flags to pass on for a device of `devType` (DIDEVCAPS::dwDevType) (exposed for tests).
DWORD Rs7Dinput_CoopFlags(DWORD devType, DWORD flags, bool background);
