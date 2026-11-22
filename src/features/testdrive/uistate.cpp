#include "features/testdrive/testdrive.h"
#include "model/gamestring.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstring>

namespace {
constexpr DWORD kPollMs = 100;
constexpr size_t kNameCap = 64;

uint32_t ReadPtr(uintptr_t a) {
    uint32_t v = 0;

    return Rsxr_SafeCopyZero(&v, reinterpret_cast<const void*>(a), 4) ? v : 0;
}

DWORD WINAPI Poller(LPVOID) {
    char logged[kNameCap] = "(none)", seen[kNameCap] = "", now[kNameCap];

    for (;;) {
        Sleep(kPollMs);
        RsxrUi_ReadState(kUiStateGlobal, now, sizeof now);

        // Logged once two polls agree: a state object being replaced can be read half-written.
        if (now[0] && !std::strcmp(now, seen) && std::strcmp(now, logged)) {
            RsxrLog("ui: %s -> %s", logged, now);
            strcpy_s(logged, now);
        }

        strcpy_s(seen, now);
    }
}
}  // namespace

bool RsxrUi_ReadState(uintptr_t global, char* out, size_t cap) {
    if (!out || !cap) {
        return false;
    }

    out[0] = 0;
    const uint32_t manager = ReadPtr(global);
    const uint32_t state = manager ? ReadPtr(manager + kUiStateCurrent) : 0;

    if (!state) {
        return false;
    }

    char buf[kNameCap];
    size_t n = 0;
    const size_t limit = cap < kNameCap ? cap : kNameCap;
    if (!rsxr::ReadGameString(reinterpret_cast<const void*>(uintptr_t(state + kUiStateName)), buf, limit, false, &n) ||
        !n) {
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

void RsxrUi_Start() {
    if (HANDLE t = CreateThread(nullptr, 0, Poller, nullptr, 0, nullptr)) {
        CloseHandle(t);
    }

    RsxrLog("ui: state watcher started (state names logged as \"ui: OLD -> NEW\")");
}
