#pragma once
#include <cstdio>
#include <vector>

namespace rs7test {
struct Case {
    const char* name;
    void (*fn)();
    const char* expectedFailure;
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
    Reg(const char* n, void (*f)(), const char* why = nullptr) { Cases().push_back({n, f, why}); }
};

inline void Fail(const char* file, int line, const char* expr) {
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    ++Failures();
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
// Strict expected failure: an unexpected pass fails the run so the marker cannot silently go stale.
#define XFAIL_TEST(name, why)                                                                                          \
    static void name();                                                                                                \
    static rs7test::Reg reg_##name(#name, name, why);                                                                  \
    static void name()
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            rs7test::Fail(__FILE__, __LINE__, #x);                                                                     \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        if (!((a) == (b)))                                                                                             \
            rs7test::Fail(__FILE__, __LINE__, #a " == " #b);                                                           \
    } while (0)
