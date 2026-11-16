// Rs7Config_ReadString's tooLong flag at the kRs7IniValueMax limit (platform/config.h).
#include "platform/config.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <string>

namespace {
constexpr size_t kMax = size_t(kRs7IniValueMax);

bool TooLongFor(size_t len, size_t* got) {
    const std::string content = "[S]\nk=" + std::string(len, 'a') + "\n";
    const std::wstring ini = rs7test::TempIni(L"toolong", content.c_str());
    bool tooLong = false;
    const std::wstring v = Rs7Config_ReadString(L"S", L"k", ini.c_str(), &tooLong);

    DeleteFileW(ini.c_str());
    *got = v.size();

    return tooLong;
}
}  // namespace

TEST(config_readstring_values_up_to_the_limit_are_not_too_long) {
    size_t got = 0;

    CHECK(!TooLongFor(1, &got));
    CHECK_EQ(got, size_t(1));
    CHECK(!TooLongFor(kMax - 1, &got));
    CHECK_EQ(got, kMax - 1);
    CHECK(!TooLongFor(kMax, &got));  // exactly the limit: read whole
    CHECK_EQ(got, kMax);
}

TEST(config_readstring_values_over_the_limit_are_flagged) {
    size_t got = 0;

    CHECK(TooLongFor(kMax + 1, &got));
    CHECK(got > kMax);
    CHECK(TooLongFor(kMax + 50, &got));
}

// The settings store's Text buffer holds exactly the longest ini value and its terminator, so both readers agree.
TEST(config_and_settings_share_one_length_limit) {
    CHECK_EQ(kRs7IniValueMax, 259);
}
