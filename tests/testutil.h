#pragma once
// Shared helpers of the native tests.
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace rsxrtest {
// A live worker may still use its fixture. Never unwind or close its handle after an unsuccessful join.
inline void Join(HANDLE thread, DWORD timeoutMs = 5000) {
    const DWORD result = WaitForSingleObject(thread, timeoutMs);

    if (result != WAIT_OBJECT_0) {
        std::printf("  FATAL: worker join failed (%lu)\n", result);
        std::fflush(stdout);
        std::abort();
    }

    CloseHandle(thread);
}

// Restores a global on scope exit, so an early return or a failed setup cannot leak the changed value into later tests.
template <class T>
class Restore {
public:
    explicit Restore(T& v) : ref_(v), saved_(v) {}

    ~Restore() { ref_ = saved_; }

    Restore(const Restore&) = delete;
    Restore& operator=(const Restore&) = delete;

private:
    T& ref_;
    T saved_;
};
}  // namespace rsxrtest
