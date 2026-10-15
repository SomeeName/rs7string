#include "test.h"

int main() {
    int expectedFailures = 0;

    for (auto& c : rs7test::Cases()) {
        const int before = rs7test::Failures();

        c.fn();

        if (c.expectedFailure) {
            if (rs7test::Failures() > before) {
                rs7test::Failures() = before;
                ++expectedFailures;
                std::printf("XFAIL %s: %s\n", c.name, c.expectedFailure);
            } else {
                rs7test::Fail(__FILE__, __LINE__, "unexpected pass: remove the XFAIL marker");
                std::printf("XPASS %s: %s\n", c.name, c.expectedFailure);
            }

            continue;
        }

        std::printf("%s %s\n", rs7test::Failures() == before ? "ok  " : "FAIL", c.name);
    }

    std::printf("%zu tests, %d failures, %d expected failures\n", rs7test::Cases().size(), rs7test::Failures(),
                expectedFailures);

    return rs7test::Failures() == 0 ? 0 : 1;
}
