#include "model/gamestring.h"
#include "test.h"
#include <cstring>

TEST(game_string_reader_preserves_length_and_bounds) {
    uint32_t s[6] = {};
    char out[8];
    const uint32_t address = uint32_t(uintptr_t(s));

    std::memcpy(s, "ab\0cd", 5);
    s[4] = address + 5;
    s[5] = address + 0x10;
    size_t length = 0;

    CHECK(rs7::ReadGameString(s, out, sizeof out, false, &length));
    CHECK_EQ(length, size_t(5));
    CHECK(std::memcmp(out, "ab\0cd\0", 6) == 0);
    CHECK(!rs7::ReadGameString(s, out, 5));
    CHECK_EQ(out[0], 0);
    CHECK(rs7::ReadGameString(s, out, 4, true, &length));
    CHECK_EQ(length, size_t(3));
    CHECK(std::memcmp(out, "ab\0\0", 4) == 0);
    CHECK(!rs7::ReadGameString(s, nullptr, 0));
    CHECK(!rs7::ReadGameString(nullptr, out, sizeof out));
    CHECK_EQ(out[0], 0);
    s[4] = address - 1;
    CHECK(!rs7::ReadGameString(s, out, sizeof out));
}
