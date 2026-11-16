#pragma once
#include <windows.h>

// One reader request at a time. Completion is fully published before a new request may reuse the event.
struct Rs7FreezeCompletion {
    HANDLE done = nullptr;
    volatile LONG busy = 0, submitted = 0, completed = 0;
    void (*hook)() = nullptr;  // tests: pause between signalling completion and publishing idle

    LONG Start();
    void Publish(LONG generation);
    bool Wait(LONG generation, unsigned timeoutMs);
};
