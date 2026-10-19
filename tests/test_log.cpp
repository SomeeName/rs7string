#include "platform/log.h"
#include "test.h"
#include <windows.h>
#include <string>

TEST(log_line_has_seconds_millis_and_newline) {
    CHECK_EQ(Rs7Log_Format(12345, "hi"), std::string("[     12.345] hi\n"));
    CHECK_EQ(Rs7Log_Format(0, "x"), std::string("[      0.000] x\n"));
}

TEST(log_before_open_is_a_noop) {
    Rs7Log("nothing happens %d", 1);  // must not crash without Rs7Log_Open
    CHECK(true);
}

namespace {
Rs7LogRedactor Me(const char* user = "Lukas") {
    Rs7LogRedactor r;

    r.profiles = {"C:\\Users\\Lukas"};
    r.users = {user};
    r.computers = {"DESKTOP-4F2K"};

    return r;
}
}  // namespace

TEST(redact_profile_prefix_becomes_a_variable) {
    CHECK_EQ(Rs7Log_Redact("config C:\\Users\\Lukas\\Games\\rs7string.ini: found", Me()),
             std::string("config %USERPROFILE%\\Games\\rs7string.ini: found"));
    CHECK_EQ(Rs7Log_Redact("at C:/Users/Lukas/x", Me()), std::string("at %USERPROFILE%/x"));
    CHECK_EQ(Rs7Log_Redact("C:\\Users\\Lukas", Me()), std::string("%USERPROFILE%"));
}

TEST(redact_ignores_case) {
    CHECK_EQ(Rs7Log_Redact("c:\\USERS\\lukas\\a and D:\\LUKAS\\b and \\\\desktop-4f2k\\s", Me()),
             std::string("%USERPROFILE%\\a and D:\\<user>\\b and \\\\<pc>\\s"));
}

TEST(redact_name_and_pc_as_path_segments) {
    CHECK_EQ(Rs7Log_Redact("D:\\backup\\Lukas\\rs.log", Me()), std::string("D:\\backup\\<user>\\rs.log"));
    CHECK_EQ(Rs7Log_Redact("\\\\DESKTOP-4F2K\\share", Me()), std::string("\\\\<pc>\\share"));
    Rs7LogRedactor pc;

    pc.computers = {"DESKTOP-1"};
    CHECK_EQ(Rs7Log_Redact("\\\\DESKTOP-1\\s", pc), std::string("\\\\<pc>\\s"));
    CHECK_EQ(Rs7Log_Redact("d:/games/Lukas/rs", Me()), std::string("d:/games/<user>/rs"));
    CHECK_EQ(Rs7Log_Redact("dir=D:\\Lukas and '\\Lukas' \\Lukas: \\Lukas", Me()),
             std::string("dir=D:\\<user> and '\\<user>' \\<user>: \\<user>"));
}

TEST(redact_profile_path_does_not_swallow_a_longer_folder) {
    CHECK_EQ(Rs7Log_Redact("C:\\Users\\Lukas2\\x", Me()), std::string("C:\\Users\\Lukas2\\x"));
}

TEST(redact_replacements_are_not_matched_again) {
    CHECK_EQ(Rs7Log_Redact("C:\\Users\\Lukas\\a", Me("USER")), std::string("%USERPROFILE%\\a"));
    CHECK_EQ(Rs7Log_Redact("C:\\Users\\Lukas\\a", Me("pro")), std::string("%USERPROFILE%\\a"));
}

TEST(redact_name_outside_a_path_segment_is_log_text) {
    const Rs7LogRedactor r = Me("Max");

    CHECK_EQ(Rs7Log_Redact("max 7 strings", r), std::string("max 7 strings"));
    CHECK_EQ(Rs7Log_Redact("kTailedMax maximum \\maximum\\x a.max Lukasz DESKTOP-4F2K", r),
             std::string("kTailedMax maximum \\maximum\\x a.max Lukasz DESKTOP-4F2K"));
    CHECK_EQ(Rs7Log_Redact("C:\\Users\\Max\\x", r), std::string("C:\\Users\\<user>\\x"));
    CHECK_EQ(Rs7Log_Redact("d:/games/max/rs", r), std::string("d:/games/<user>/rs"));
}

TEST(redact_short_name_is_a_path_segment_like_any_other) {
    const Rs7LogRedactor r = Me("al");

    CHECK_EQ(Rs7Log_Redact("final alpha a.al al", r), std::string("final alpha a.al al"));
    CHECK_EQ(Rs7Log_Redact("D:\\al\\x and D:\\al", r), std::string("D:\\<user>\\x and D:\\<user>"));
    CHECK_EQ(Rs7Log_Redact("D:\\alpha\\x \\final", r), std::string("D:\\alpha\\x \\final"));
    CHECK_EQ(Rs7Log_Redact("\"D:\\al\"", r), std::string("\"D:\\<user>\""));
}

TEST(redact_empty_names_and_empty_redactor_change_nothing) {
    const std::string line = "C:\\Users\\Lukas\\x DESKTOP-4F2K";
    const Rs7LogRedactor none;
    Rs7LogRedactor blanks;

    blanks.profiles = {""};
    blanks.users = {""};
    blanks.computers = {""};

    CHECK_EQ(Rs7Log_Redact(line, none), line);
    CHECK_EQ(Rs7Log_Redact(line, blanks), line);
    CHECK_EQ(Rs7Log_Redact("", Me()), std::string());
}

TEST(redact_current_identity_hides_the_real_profile) {
    char profile[MAX_PATH] = {};

    if (GetEnvironmentVariableA("USERPROFILE", profile, sizeof profile) && profile[0]) {
        const std::string line = std::string("path ") + profile + "\\rs7string.ini";
        const std::string out = Rs7Log_Redact(line, Rs7Log_CurrentIdentity());

        CHECK(out.find(profile) == std::string::npos);
        CHECK(out.find("rs7string.ini") != std::string::npos);
    }
}
