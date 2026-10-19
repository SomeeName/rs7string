#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Portable, pure copy/insert interpreter. No Windows, game, filesystem or keys.
namespace rs7hs {
using Bytes = std::vector<uint8_t>;

struct File {
    std::string name;
    Bytes bytes;
};

struct Expected {
    std::string name;
    uint32_t size;
    std::string sha256;
};

std::string Hash(const Bytes& bytes);
// All failures clear outputs, return false and provide a diagnostic. Bounded to 64 MiB.
bool Build(const Bytes& guitar, const Bytes& peg, const Bytes& package, std::vector<File>& outputs, std::string& error);
bool Manifest(const Bytes& package, std::vector<Expected>& outputs, std::string& error);
bool CheckTemplates(const Bytes& guitar, const Bytes& peg, const Bytes& package, std::string& error);
}  // namespace rs7hs
