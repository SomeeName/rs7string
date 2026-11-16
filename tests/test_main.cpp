#include "test.h"
#include <exception>

namespace {
// Runs one test; an escaping C++ exception is a failure, not a crash that loses the summary.
void Run(const rs7test::Case& c) {
    try {
        c.fn();
    } catch (const std::exception& e) {
        std::printf("  exception: %s\n", e.what());
        rs7test::Fail(__FILE__, __LINE__, "test threw a C++ exception");
    } catch (...) {
        rs7test::Fail(__FILE__, __LINE__, "test threw a C++ exception");
    }
}
}  // namespace

int main() {
    int expectedFailures = 0, skipped = 0;

    for (auto& c : rs7test::Cases()) {
        const int before = rs7test::Failures(), checksBefore = rs7test::Checks();

        rs7test::Skipped() = false;

        for (int attempt = 1; attempt <= c.attempts; ++attempt) {
            rs7test::SetupRetry() = nullptr;
            Run(c);
            if (!rs7test::SetupRetry() || rs7test::Failures() > before) {
                break;
            }

            if (attempt == c.attempts) {
                rs7test::Fail(__FILE__, __LINE__, rs7test::SetupRetry());
                break;
            }

            std::printf("RETRY %s (attempt %d of %d: %s)\n", c.name, attempt + 1, c.attempts, rs7test::SetupRetry());
        }

        const bool ranChecks = rs7test::Checks() > checksBefore;

        // A test that evaluated no CHECK and did not Skip proved nothing: its setup returned early or it lost its
        // assertions. An expected failure must fail a real CHECK too, so this does not count as its failure.
        if (!ranChecks && !rs7test::Skipped() && !c.expectedFailure) {
            rs7test::Fail(__FILE__, __LINE__, "no CHECK ran (call rs7test::Skip for an environment skip)");
        }

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

        const bool failed = rs7test::Failures() > before;

        if (!failed && rs7test::Skipped()) {
            ++skipped;
        }

        std::printf("%s %s\n", failed ? "FAIL" : !ranChecks ? "skip" : "ok  ", c.name);
    }

    std::printf("%zu tests, %d failures, %d expected failures, %d skipped\n", rs7test::Cases().size(),
                rs7test::Failures(), expectedFailures, skipped);

    return rs7test::Failures() == 0 ? 0 : 1;
}
