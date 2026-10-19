// libFuzzer entry points: the target named by RS7_FUZZ_TARGET (fuzz.h).
#include "fuzz.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rs7fuzz {
const Target kTargets[] = {
    {"rs7x", Rs7x, Rs7xSeeds}, {"dxt", Dxt, DxtSeeds},    {"settings", Settings, SettingsSeeds},
    {"ini", Ini, IniSeeds},    {"text", Text, TextSeeds}, {"package", Package, NoSeeds},
    {"nif", Nif, NoSeeds},     {"dds", Dds, DdsSeeds},    {"recipe", Recipe, RecipeSeeds},
};
const size_t kTargetCount = sizeof kTargets / sizeof kTargets[0];

std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::wstring Widen(const uint8_t* d, size_t n) {
    std::wstring w;

    for (size_t i = 0; i + 1 < n; i += 2) {
        const wchar_t c = wchar_t(d[i] | d[i + 1] << 8);

        if (!c) {
            break;
        }

        w += c;
    }

    return w;
}

Seeds NoSeeds() {
    return {};
}
}  // namespace rs7fuzz

namespace {
const rs7fuzz::Target* g_target = nullptr;

void WriteSeeds(const rs7fuzz::Target& t, const char* dir) {
    const rs7fuzz::Seeds seeds = t.seeds();

    for (size_t i = 0; i < seeds.size(); ++i) {
        char path[512];

        std::snprintf(path, sizeof path, "%s/builtin-%02u", dir, unsigned(i));

        FILE* f = std::fopen(path, "wb");

        if (!f) {
            std::fprintf(stderr, "%s: cannot write %s\n", t.name, path);
            std::exit(1);
        }

        const bool ok = std::fwrite(seeds[i].data(), 1, seeds[i].size(), f) == seeds[i].size();

        if (std::fclose(f) != 0 || !ok) {
            std::fprintf(stderr, "%s: writing %s failed\n", t.name, path);
            std::exit(1);
        }
    }

    std::printf("%s: %u built-in seeds in %s\n", t.name, unsigned(seeds.size()), dir);
}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    const char* name = std::getenv("RS7_FUZZ_TARGET");

    for (size_t i = 0; name && i < rs7fuzz::kTargetCount; ++i) {
        if (!std::strcmp(name, rs7fuzz::kTargets[i].name)) {
            g_target = &rs7fuzz::kTargets[i];
        }
    }

    if (!g_target) {
        std::fprintf(stderr, "set RS7_FUZZ_TARGET to one of:");

        for (size_t i = 0; i < rs7fuzz::kTargetCount; ++i) {
            std::fprintf(stderr, " %s", rs7fuzz::kTargets[i].name);
        }

        std::fprintf(stderr, "\n");
        std::exit(2);
    }

    if (const char* dir = std::getenv("RS7_FUZZ_SEEDS")) {
        WriteSeeds(*g_target, dir);
        std::exit(0);
    }

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return g_target->run(data, size);
}
