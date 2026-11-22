#include "test.h"
#include <exception>

namespace {
// Runs one test; an escaping C++ exception is a failure, not a crash that loses the summary.
void Run(const rsxrtest::Case& c) {
    try {
        c.fn();
    } catch (const std::exception& e) {
        std::printf("  exception: %s\n", e.what());
        rsxrtest::Fail(__FILE__, __LINE__, "test threw a C++ exception");
    } catch (...) {
        rsxrtest::Fail(__FILE__, __LINE__, "test threw a C++ exception");
    }
}
}  // namespace

int main() {
    int expectedFailures = 0, skipped = 0;

    for (auto& c : rsxrtest::Cases()) {
        const int before = rsxrtest::Failures(), checksBefore = rsxrtest::Checks();

        rsxrtest::Skipped() = false;

        for (int attempt = 1; attempt <= c.attempts; ++attempt) {
            rsxrtest::SetupRetry() = nullptr;
            Run(c);
            if (!rsxrtest::SetupRetry() || rsxrtest::Failures() > before) {
                break;
            }

            if (attempt == c.attempts) {
                rsxrtest::Fail(__FILE__, __LINE__, rsxrtest::SetupRetry());
                break;
            }

            std::printf("RETRY %s (attempt %d of %d: %s)\n", c.name, attempt + 1, c.attempts, rsxrtest::SetupRetry());
        }

        const bool ranChecks = rsxrtest::Checks() > checksBefore;

        // A test that evaluated no CHECK and did not Skip proved nothing: its setup returned early or it lost its
        // assertions. An expected failure must fail a real CHECK too, so this does not count as its failure.
        if (!ranChecks && !rsxrtest::Skipped() && !c.expectedFailure) {
            rsxrtest::Fail(__FILE__, __LINE__, "no CHECK ran (call rsxrtest::Skip for an environment skip)");
        }

        if (c.expectedFailure) {
            if (rsxrtest::Failures() > before) {
                rsxrtest::Failures() = before;
                ++expectedFailures;
                std::printf("XFAIL %s: %s\n", c.name, c.expectedFailure);
            } else {
                rsxrtest::Fail(__FILE__, __LINE__, "unexpected pass: remove the XFAIL marker");
                std::printf("XPASS %s: %s\n", c.name, c.expectedFailure);
            }

            continue;
        }

        const bool failed = rsxrtest::Failures() > before;

        if (!failed && rsxrtest::Skipped()) {
            ++skipped;
        }

        std::printf("%s %s\n", failed ? "FAIL" : !ranChecks ? "skip" : "ok  ", c.name);
    }

    std::printf("%zu tests, %d failures, %d expected failures, %d skipped\n", rsxrtest::Cases().size(),
                rsxrtest::Failures(), expectedFailures, skipped);

    return rsxrtest::Failures() == 0 ? 0 : 1;
}
