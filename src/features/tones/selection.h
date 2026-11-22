#pragma once
#include <cstdint>

namespace rsxrtones {
constexpr uint32_t kSlots = 26, kBase = 26, kInvalid = 0xFFFFFFFFu;

constexpr uint32_t Select(bool event, uint32_t raw) {
    return !event ? kBase : raw < kSlots ? raw : kInvalid;
}

struct Player {
    uint32_t session = 0, selected = kInvalid;

    bool Needs(uint32_t owner, uint32_t id) const { return session != owner || selected != id; }

    void Applied(uint32_t owner, uint32_t id) {
        session = owner;
        selected = id;
    }
};
}  // namespace rsxrtones
