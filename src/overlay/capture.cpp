#include "overlay/capture.h"

namespace {
volatile bool g_mouse, g_keys;
}  // namespace

void RsxrOverlay_SetCapture(bool mouse, bool keys) {
    g_mouse = mouse;
    g_keys = keys;
}

bool RsxrOverlay_WantsMouse() {
    return g_mouse;
}

bool RsxrOverlay_WantsKeys() {
    return g_keys;
}
