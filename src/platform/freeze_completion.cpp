#include "platform/freeze_completion.h"

LONG RsxrFreezeCompletion::Start() {
    ResetEvent(done);
    InterlockedExchange(&busy, 1);

    return InterlockedIncrement(&submitted);
}

void RsxrFreezeCompletion::Publish(LONG generation) {
    InterlockedExchange(&completed, generation);
    SetEvent(done);
    if (hook) {
        hook();
    }
    InterlockedExchange(&busy, 0);
}

bool RsxrFreezeCompletion::Wait(LONG generation, unsigned timeoutMs) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;

    for (;;) {
        // The event can wake us before Publish's final store, or remain signalled after a wedged job.
        // Accept only this request's result, after the reader has stopped touching shared state.
        if (!InterlockedCompareExchange(&busy, 0, 0) && InterlockedCompareExchange(&completed, 0, 0) == generation) {
            return true;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= end) {
            return false;
        }

        const DWORD remaining = static_cast<DWORD>(end - now);

        WaitForSingleObject(done, remaining < 2 ? remaining : 2);
    }
}
