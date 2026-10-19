#pragma once

// Whether the visible F9 overlay uses the mouse (pointer over a window, a drag) or the keyboard (a text
// field has focus) right now. The render thread sets it each frame, and clears it when the overlay is
// hidden. The DirectInput wrapper reads it to hide that input from the game (dinput_wrap.h). Any thread.
void Rs7Overlay_SetCapture(bool mouse, bool keys);
bool Rs7Overlay_WantsMouse();
bool Rs7Overlay_WantsKeys();
