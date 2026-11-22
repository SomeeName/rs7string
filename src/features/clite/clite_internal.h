// Private to features/clite: state and helpers shared by hooks.cpp and watcher.cpp.
#pragma once

#include "features/clite/clite.h"
#include "platform/field.h"
#include <windows.h>

namespace rsxrclite::internal {
// Log line cap of the clite log lines (g_lines); the watcher stops once it is reached.
constexpr long kLogCap = 400;
extern volatile long g_lines;
extern volatile LONG64 g_selQpc;
extern volatile uint32_t g_loadPlayer;
extern volatile long g_rebuilds[2];  // per player
extern LARGE_INTEGER g_freq;

double MsSince(LONG64 t0);
LONG64 Now();

using rsxrmem::UncheckedRead;

struct Snap {
    uintptr_t holder = 0, guitar = 0;
    int state = -1;
    int count = -1;
    int type = -1;
};

Snap Read(uintptr_t mgr, unsigned slot);
void Attach(uintptr_t holder);

// Starts the log-only rebuild timing thread (watcher.cpp).
void StartWatcher();
}  // namespace rsxrclite::internal
