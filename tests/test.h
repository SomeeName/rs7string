#pragma once
#include <cstdio>
#include <cstdlib>
#include <sal.h>
#include <vector>

namespace rs7test {
struct Case {
    const char* name;
    void (*fn)();
    const char* expectedFailure;
    int attempts;  // TIMING_TEST: retries only an explicitly refused setup, never a failed CHECK or exception
};

inline std::vector<Case>& Cases() {
    static std::vector<Case> c;

    return c;
}

inline int& Failures() {
    static int f = 0;

    return f;
}

struct Reg {
    Reg(const char* n, void (*f)(), const char* why = nullptr, int attempts = 1) {
        Cases().push_back({n, f, why, attempts});
    }
};

// CHECKs evaluated so far: a test that ran none (and did not Skip) fails, so a setup that returns early or a loop
// over an empty set cannot pass silently.
inline int& Checks() {
    static int c = 0;

    return c;
}

// Set by Skip for the running test: the environment lacks a device or asset files. Counted and reported.
inline bool& Skipped() {
    static bool s = false;

    return s;
}

inline void Fail(const char* file, int line, const char* expr) {
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    ++Failures();
}

inline void Skip(const char* why) {
    std::printf("  SKIP %s\n", why);
    Skipped() = true;
}

inline const char*& SetupRetry() {
    static const char* reason = nullptr;

    return reason;
}

// Only before the operation under test: return from the test when the environment is not ready.
inline bool Ready(bool ready, const char* why) {
    if (!ready) {
        SetupRetry() = why;
    }

    return ready;
}

// A resource a test cannot run without (VirtualAlloc, CreateThread, a locked level's bits): stops the run with a
// message instead of crashing on the null pointer later, and tells /analyze the result is non-null.
template <class T>
_Ret_notnull_ T* Must(T* p, const char* what) {
    if (!p) {
        std::printf("  FATAL: %s failed\n", what);
        std::abort();
    }

    return p;
}
}  // namespace rs7test

#define TEST(name)                                                                                                     \
    static void name();                                                                                                \
    static rs7test::Reg reg_##name(#name, name);                                                                       \
    static void name()
// A test whose inline asm uses EBX, which AddressSanitizer needs as its frame pointer (C5073): not instrumented
// in the -Asan build (build.ps1); a plain TEST everywhere else.
#ifdef __SANITIZE_ADDRESS__
#define RS7_NO_ASAN __declspec(no_sanitize_address)
#else
#define RS7_NO_ASAN
#endif
#define TEST_NO_ASAN(name)                                                                                             \
    static void name();                                                                                                \
    static rs7test::Reg reg_##name(#name, name);                                                                       \
    RS7_NO_ASAN static void name()
// Up to three attempts when Ready refuses setup before the operation under test. CHECKs and exceptions never retry.
#define TIMING_TEST(name)                                                                                              \
    static void name();                                                                                                \
    static rs7test::Reg reg_##name(#name, name, nullptr, 3);                                                           \
    static void name()
// Strict expected failure: an unexpected pass fails the run so the marker cannot silently go stale.
#define XFAIL_TEST(name, why)                                                                                          \
    static void name();                                                                                                \
    static rs7test::Reg reg_##name(#name, name, why);                                                                  \
    static void name()
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++rs7test::Checks();                                                                                           \
        if (!(x))                                                                                                      \
            rs7test::Fail(__FILE__, __LINE__, #x);                                                                     \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++rs7test::Checks();                                                                                           \
        if (!((a) == (b)))                                                                                             \
            rs7test::Fail(__FILE__, __LINE__, #a " == " #b);                                                           \
    } while (0)
