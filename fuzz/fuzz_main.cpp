// libFuzzer entry points: the target named by RSXR_FUZZ_TARGET (fuzz.h).
#include "fuzz.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rsxrfuzz {
const Target kTargets[] = {
    {"extblock", Extblock, ExtblockSeeds},
    {"settings", Settings, SettingsSeeds},
    {"ini", Ini, IniSeeds},
    {"text", Text, TextSeeds},
    {"package", Package, NoSeeds},
    {"nif", Nif, NoSeeds},
    {"dds", Dds, DdsSeeds},
    {"recipe", Recipe, RecipeSeeds},
    {"nslot", Nslot, NslotSeeds},
    {"plain", Plain, PlainSeeds},
    {"pattern", Pattern, PatternSeeds},
    {"tuningname", TuningName, TuningNameSeeds},
    {"redact", Redact, RedactSeeds},
    {"nativesng", NativeSng, NativeSngSeeds},
    {"nativemanifest", NativeManifest, NativeManifestSeeds},
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
}  // namespace rsxrfuzz

namespace {
const rsxrfuzz::Target* g_target = nullptr;

void WriteSeeds(const rsxrfuzz::Target& t, const char* dir) {
    const rsxrfuzz::Seeds seeds = t.seeds();

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
    const char* name = std::getenv("RSXR_FUZZ_TARGET");

    for (size_t i = 0; name && i < rsxrfuzz::kTargetCount; ++i) {
        if (!std::strcmp(name, rsxrfuzz::kTargets[i].name)) {
            g_target = &rsxrfuzz::kTargets[i];
        }
    }

    if (!g_target) {
        std::fprintf(stderr, "set RSXR_FUZZ_TARGET to one of:");

        for (size_t i = 0; i < rsxrfuzz::kTargetCount; ++i) {
            std::fprintf(stderr, " %s", rsxrfuzz::kTargets[i].name);
        }

        std::fprintf(stderr, "\n");
        std::exit(2);
    }

    if (const char* dir = std::getenv("RSXR_FUZZ_SEEDS")) {
        WriteSeeds(*g_target, dir);
        std::exit(0);
    }

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return g_target->run(data, size);
}
