#include "overlay/capture.h"

namespace {
volatile bool g_mouse, g_keys;
}  // namespace

void Rs7Overlay_SetCapture(bool mouse, bool keys) {
    g_mouse = mouse;
    g_keys = keys;
}

bool Rs7Overlay_WantsMouse() {
    return g_mouse;
}

bool Rs7Overlay_WantsKeys() {
    return g_keys;
}
