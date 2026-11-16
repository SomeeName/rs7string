#include "platform/safecopy.h"
#include "test.h"
#include <windows.h>
#include <cstdint>

TEST(safe_read_reads_at_offset) {
    const uint8_t bytes[] = {0x11, 0x78, 0x56, 0x34, 0x12};

    CHECK_EQ(Rs7_SafeRead<uint32_t>(bytes, 1), uint32_t(0x12345678));
    CHECK_EQ(Rs7_SafeRead<uint8_t>(bytes, 0), uint8_t(0x11));
    CHECK_EQ(Rs7_SafeRead<uint8_t>(nullptr, size_t(uintptr_t(bytes))), uint8_t(0));
}

TEST_NO_ASAN(safe_read_is_zero_on_a_fault) {
    void* page = rs7test::Must(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS), "VirtualAlloc");

    CHECK_EQ(Rs7_SafeRead<uint32_t>(page, 8), uint32_t(0));
    CHECK_EQ(Rs7_SafeRead<uint32_t>(nullptr, 0), uint32_t(0));
    VirtualFree(page, 0, MEM_RELEASE);
}
