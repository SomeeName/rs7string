#define main RunTests
#include "../../../tests/test_main.cpp"
#undef main
#include <cstring>
#include <stdexcept>

namespace {
const char* g_mode = nullptr;
int g_attempt = 0;

void Attempt() {
    ++g_attempt;
    if (std::strcmp(g_mode, "exception") == 0 && g_attempt == 1) {
        throw std::runtime_error("correctness failure");
    }

    if (std::strcmp(g_mode, "correctness") == 0) {
        CHECK(g_attempt > 1);
        return;
    }

    if (std::strcmp(g_mode, "mixed") == 0) {
        CHECK(g_attempt > 1);
    }
    if (!rsxrtest::Ready(g_attempt > 1 && std::strcmp(g_mode, "exhausted") != 0, "worker not ready")) {
        return;
    }

    CHECK(true);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 90;
    }

    g_mode = argv[1];
    rsxrtest::Reg reg("timing_case", &Attempt, nullptr, 3);
    const int result = RunTests();

    std::printf("attempts=%d\n", g_attempt);

    return result;
}
