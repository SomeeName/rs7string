#pragma once
#include <array>
#include <cstdint>

namespace rsxrtones {
// Owns only isolated resource vectors. Never installed in the game's live manager.
class NativeRegistration {
public:
    NativeRegistration() = default;
    ~NativeRegistration();
    NativeRegistration(const NativeRegistration&) = delete;
    NativeRegistration& operator=(const NativeRegistration&) = delete;
    bool Register(uint32_t definition, uint32_t player, uint32_t slot);

private:
    struct Record {
        uint32_t begin = 0, end = 0, capacity = 0;
    };

    static_assert(sizeof(Record) == 12);
    std::array<Record, 62> records_{};  // eight reserved records, two rows of Base/A..Z
    uintptr_t destroy_ = 0;
};
}  // namespace rsxrtones
