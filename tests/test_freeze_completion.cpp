#include "platform/freeze_completion.h"
#include "test.h"
#include <windows.h>

namespace {
HANDLE g_publishing = nullptr, g_release = nullptr;

void Pause() {
    SetEvent(g_publishing);
    WaitForSingleObject(g_release, 5000);
}

struct PublishJob {
    Rs7FreezeCompletion* completion;
    LONG generation;
};

DWORD WINAPI Publish(void* p) {
    auto& job = *static_cast<PublishJob*>(p);

    job.completion->Publish(job.generation);

    return 0;
}
}  // namespace

TIMING_TEST(freeze_completion_waits_for_idle_and_for_the_current_generation) {
    Rs7FreezeCompletion completion;

    completion.done = rs7test::Must(CreateEventW(nullptr, FALSE, FALSE, nullptr), "CreateEvent");
    g_publishing = rs7test::Must(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEvent");
    g_release = rs7test::Must(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEvent");
    completion.hook = Pause;
    PublishJob job{&completion, completion.Start()};
    const HANDLE reader = rs7test::Must(CreateThread(nullptr, 0, Publish, &job, 0, nullptr), "CreateThread");

    CHECK_EQ(WaitForSingleObject(g_publishing, 5000), WAIT_OBJECT_0);
    CHECK(!completion.Wait(job.generation, 0));
    CHECK_EQ(InterlockedCompareExchange(&completion.busy, 0, 0), LONG(1));
    SetEvent(g_release);
    CHECK_EQ(WaitForSingleObject(reader, 5000), WAIT_OBJECT_0);
    CHECK(completion.Wait(job.generation, 0));
    completion.hook = nullptr;
    const LONG next = completion.Start();

    // A stale event and even an idle reader cannot substitute the preceding request's result.
    SetEvent(completion.done);
    InterlockedExchange(&completion.busy, 0);
    CHECK(!completion.Wait(next, 0));
    completion.Publish(next);
    CHECK(completion.Wait(next, 0));
    CloseHandle(reader);
    CloseHandle(completion.done);
    CloseHandle(g_publishing);
    CloseHandle(g_release);
    g_publishing = g_release = nullptr;
}
