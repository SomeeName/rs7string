#pragma once

// Whether the visible F9 overlay uses the mouse (pointer over a window, a drag) or the keyboard (a text
// field has focus) right now. The render thread sets it each frame, and clears it when the overlay is
// hidden. The DirectInput wrapper reads it to hide that input from the game (dinput_wrap.h). Any thread.
void RsxrOverlay_SetCapture(bool mouse, bool keys);
bool RsxrOverlay_WantsMouse();
bool RsxrOverlay_WantsKeys();
