#include "features/scene/scene.h"
#include "features/testdrive/testdrive.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

namespace {
constexpr DWORD kPollMs = 100;
constexpr size_t kNameCap = 64;

using rs7s7d::SafeCopy;

uint32_t ReadPtr(uintptr_t a) {
    uint32_t v = 0;

    return SafeCopy(&v, reinterpret_cast<const void*>(a), 4) ? v : 0;
}

DWORD WINAPI Poller(LPVOID) {
    char logged[kNameCap] = "(none)", seen[kNameCap] = "", now[kNameCap];

    for (;;) {
        Sleep(kPollMs);
        Rs7Ui_ReadState(kUiStateGlobal, now, sizeof now);

        // Logged once two polls agree: a state object being replaced can be read half-written.
        if (now[0] && !std::strcmp(now, seen) && std::strcmp(now, logged)) {
            Rs7Log("ui: %s -> %s", logged, now);
            strcpy_s(logged, now);
        }

        strcpy_s(seen, now);
    }
}
}  // namespace

bool Rs7Ui_ReadState(uintptr_t global, char* out, size_t cap) {
    out[0] = 0;
    const uint32_t manager = ReadPtr(global);
    const uint32_t state = manager ? ReadPtr(manager + kUiStateCurrent) : 0;

    if (!state) {
        return false;
    }

    const uintptr_t str = state + kUiStateName;
    const uint32_t text = ReadPtr(str + 0x14) == str + 0x10 ? uint32_t(str) : ReadPtr(str);
    const uint32_t end = ReadPtr(str + 0x10);

    if (!text || end <= text || end - text >= cap || end - text >= kNameCap) {
        return false;
    }

    char buf[kNameCap];
    const size_t n = end - text;

    if (!SafeCopy(buf, reinterpret_cast<const void*>(uintptr_t(text)), n)) {
        return false;
    }

    for (size_t i = 0; i < n; ++i) {
        if (buf[i] < 0x20 || buf[i] > 0x7E) {
            return false;
        }
    }

    std::memcpy(out, buf, n);
    out[n] = 0;

    return true;
}

void Rs7Ui_Start() {
    if (HANDLE t = CreateThread(nullptr, 0, Poller, nullptr, 0, nullptr)) {
        CloseHandle(t);
    }

    Rs7Log("ui: state watcher started (state names logged as \"ui: OLD -> NEW\")");
}
