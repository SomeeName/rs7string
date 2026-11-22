#include "platform/field.h"
#include "test.h"
#include <cstdint>

TEST(unchecked_fields_alias_storage_and_read_unaligned_values) {
    alignas(4) uint8_t bytes[12] = {};

    rsxrmem::UncheckedField<uint32_t>(bytes, 4) = 0x12345678;
    CHECK_EQ(rsxrmem::UncheckedRead<uint32_t>(bytes, 4), 0x12345678u);
    CHECK_EQ(rsxrmem::UncheckedRead<uint8_t>(bytes, 5), 0x56u);
    CHECK_EQ(rsxrmem::UncheckedRead<uint16_t>(bytes, 5), 0x3456u);
    CHECK_EQ(rsxrmem::UncheckedRead<uint32_t>(uintptr_t(bytes + 4)), 0x12345678u);
}
