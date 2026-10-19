#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The parser fuzz targets (scripts\fuzz.ps1, docs/DEVELOPMENT.md "Fuzzing"). One libFuzzer exe; RS7_FUZZ_TARGET
// picks the target, RS7_FUZZ_SEEDS=DIR writes the target's built-in seeds into DIR and exits.
namespace rs7fuzz {
using Seeds = std::vector<std::vector<uint8_t>>;

struct Target {
    const char* name;
    int (*run)(const uint8_t* data, size_t size);
    Seeds (*seeds)();  // built-in seeds (files from the repository are added by scripts\fuzz.ps1)
};

// Every target, in fuzz.ps1's order.
extern const Target kTargets[];
extern const size_t kTargetCount;

int Rs7x(const uint8_t* d, size_t n);
int Dxt(const uint8_t* d, size_t n);
int Settings(const uint8_t* d, size_t n);
int Ini(const uint8_t* d, size_t n);
int Text(const uint8_t* d, size_t n);
int Package(const uint8_t* d, size_t n);
int Nif(const uint8_t* d, size_t n);
int Dds(const uint8_t* d, size_t n);
int Recipe(const uint8_t* d, size_t n);
Seeds Rs7xSeeds();
Seeds DxtSeeds();
Seeds SettingsSeeds();
Seeds IniSeeds();
Seeds TextSeeds();
Seeds NoSeeds();
Seeds DdsSeeds();
Seeds RecipeSeeds();

// Helpers for the targets.
std::vector<uint8_t> Bytes(const std::string& s);
std::wstring Widen(const uint8_t* d, size_t n);  // bytes as UTF-16 code units (0 ends the text)
}  // namespace rs7fuzz
