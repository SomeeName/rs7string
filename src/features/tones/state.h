#pragma once
#include "features/tones/selection.h"
#include <array>
#include <memory>
#include <string>

namespace rsxrtones {
using Identity = std::array<uint32_t, 4>;

struct Tone {
    std::string key;
    uint32_t object = 0;

    Tone() = default;
    Tone(const Tone&) = delete;
    Tone& operator=(const Tone&) = delete;
    ~Tone();
};

struct Arrangement {
    Identity id{};
    std::array<Tone, 22> extra;
    std::string slots;
    const char* inactive = "no extra tones";
};

void Capture(uint32_t attributes, uint32_t collection, const Identity& id);
void PreRegister(uint32_t session);
// True means a native arrangement with extra slots owns this consumer, including rejected IDs.
bool Handle(uint32_t session, bool event, uint32_t raw, bool force);
}  // namespace rsxrtones
