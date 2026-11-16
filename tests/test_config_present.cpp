#include "platform/config.h"
#include "settings_removed.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <string>

// Rs7Config_ReadPresent tells a missing key from an empty one; Rs7Config_ReadInt and the removed-key read
// (Rs7Removed_ReadIni) build on it.
TEST(config_read_present_tells_missing_from_empty_and_present) {
    const std::wstring p = rs7test::TempIni(L"present", "[s]\r\nempty=\r\nset=1\r\nspaced= 7 \r\n");
    std::wstring v = L"unchanged";

    CHECK(!Rs7Config_ReadPresent(L"s", L"missing", p.c_str(), &v));
    CHECK(v == L"unchanged");  // a missing key leaves the output alone
    CHECK(!Rs7Config_ReadPresent(L"other", L"set", p.c_str(), &v));
    CHECK(Rs7Config_ReadPresent(L"s", L"empty", p.c_str(), &v));
    CHECK(v.empty());
    CHECK(Rs7Config_ReadPresent(L"s", L"set", p.c_str(), &v));
    CHECK(v == L"1");
    CHECK(Rs7Config_ReadPresent(L"s", L"spaced", p.c_str(), &v));
    CHECK(v == L"7");  // the API trims the value
    CHECK(!Rs7Config_ReadPresent(L"s", L"set", LR"(Z:\does\not\exist\rs7string.ini)", &v));

    // Missing: the default; empty: invalid (0); present: the value.
    CHECK_EQ(Rs7Config_ReadInt(L"s", L"missing", 1, p.c_str()), 1);
    CHECK_EQ(Rs7Config_ReadInt(L"s", L"empty", 1, p.c_str()), 0);
    CHECK_EQ(Rs7Config_ReadInt(L"s", L"set", 0, p.c_str()), 1);
    CHECK_EQ(Rs7Config_ReadInt(L"s", L"spaced", 0, p.c_str(), 9), 7);
    DeleteFileW(p.c_str());
}

TEST(config_removed_keys_count_empty_values_and_skip_missing_ones) {
    const std::wstring none = rs7test::TempIni(L"removed_none", "[debug]\r\ndiagnostics=1\r\n");

    CHECK_EQ(Rs7Removed_ReadIni(none.c_str()), 0);
    DeleteFileW(none.c_str());
    const std::wstring empty = rs7test::TempIni(L"removed_empty", "[color]\r\nramp_swap=\r\n");

    CHECK_EQ(Rs7Removed_ReadIni(empty.c_str()), 1);  // present without a value still counts
    DeleteFileW(empty.c_str());
    CHECK_EQ(Rs7Removed_ReadIni(nullptr), 0);
}
