#include "config_fixture.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

TEST(config_missing_file_uses_the_native_defaults) {
    const RsxrTestConfig c = RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini");

    CHECK(!c.found);
    CHECK(c.tunerLowRange);
    CHECK_EQ(c.tunerLowRangeRaw, 1);
    CHECK_EQ(c.removedKeys, 0);
    CHECK(!c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 0);
}

TEST(config_removed_keys_are_ignored_and_counted) {
    // 1.0: an old ini with a removed legacy key still loads; the key changes nothing and is only logged.
    const std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\nstring6_as_string0=1\r\ndiagnostics=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.found);
    CHECK_EQ(c.removedKeys, 1);
    CHECK(c.diagnostics);
    DeleteFileW(p.c_str());
}

TEST(config_keys_removed_with_the_settings_cleanup_are_counted) {
    const std::wstring p = rsxrtest::TempIni(
        L"config", "[neck]\r\nstring7_v=0.3\r\nfret_extend=0\r\nfret_halflen=4.25\r\nheadstock_peg=0\r\n[color]\r\n"
                   "ramp_swap=0\r\nallow_inexact=1\r\n[chords]\r\nframe_scale=1.2\r\nframe_axis=0\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK_EQ(c.removedKeys, 8);
    CHECK(c.string7Line);
    DeleteFileW(p.c_str());
}

TEST(config_removed_keys_table_lists_each_key_once) {
    for (int i = 0; i < kRsxrRemovedKeyCount; ++i) {
        for (int j = i + 1; j < kRsxrRemovedKeyCount; ++j) {
            CHECK(std::wcscmp(kRsxrRemovedKeys[i].section, kRsxrRemovedKeys[j].section) ||
                  std::wcscmp(kRsxrRemovedKeys[i].key, kRsxrRemovedKeys[j].key));
        }
    }
}

TEST(config_diagnostics_default_off) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\nstring6_as_string0=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.found);
    CHECK(!c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 0);
    DeleteFileW(p.c_str());
}

TEST(config_diagnostics_on) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\ndiagnostics=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.found);
    CHECK(c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 1);
    DeleteFileW(p.c_str());
}

TEST(config_diagnostics_off_explicitly) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\ndiagnostics=0\r\nstring6_as_string0=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(!c.diagnostics);
    CHECK_EQ(c.removedKeys, 1);
    DeleteFileW(p.c_str());
}

TEST(config_color_defaults) {
    const RsxrTestConfig c = RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini");

    CHECK_EQ(c.string6Rgb, 0xC8C8C8u);
    CHECK_EQ(c.string6RgbCb, 0xC8C8C8u);
    CHECK(c.colorValid && !c.colorCbSet);
}

TEST(config_color_cb_defaults_to_the_normal_colour) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[color]\r\nstring6_color=FF00FF\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK_EQ(c.string6Rgb, 0xFF00FFu);
    CHECK_EQ(c.string6RgbCb, 0xFF00FFu);
    CHECK(c.colorValid && !c.colorCbSet && c.colorCbValid && c.string6Raw == L"FF00FF");
    DeleteFileW(p.c_str());
}

TEST(config_color_cb_and_the_removed_ramp_switches) {
    const std::wstring p = rsxrtest::TempIni(
        L"config", "[color]\r\nstring6_color=FF00FF\r\nstring6_color_cb=00FFFF\r\nramp_swap=0\r\nallow_inexact=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK_EQ(c.string6RgbCb, 0x00FFFFu);
    CHECK(c.colorCbSet && c.colorCbValid);
    CHECK_EQ(c.removedKeys, 2);  // ramp_swap, allow_inexact: removed before 1.0, ignored
    DeleteFileW(p.c_str());
}

TEST(config_invalid_colour_falls_back_to_default_and_keeps_the_raw_text) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[color]\r\nstring6_color=red\r\nstring6_color_cb=12345\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u && c.string6Raw == L"red");
    CHECK(!c.colorCbValid && c.string6RgbCb == 0xC8C8C8u);  // invalid cb -> the (valid or default) normal colour
    CHECK(c.colorCbSet && c.string6CbRaw == L"12345");
    DeleteFileW(p.c_str());
}

TEST(config_invalid_cb_uses_the_valid_normal_colour) {
    const std::wstring p =
        rsxrtest::TempIni(L"config", "[color]\r\nstring6_color=#102030\r\nstring6_color_cb=nope\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.colorValid && c.string6Rgb == 0x102030u);
    CHECK(c.colorCbSet && !c.colorCbValid && c.string6RgbCb == 0x102030u);
    DeleteFileW(p.c_str());
}

TEST(config_empty_colour_keeps_the_default_as_valid) {
    const std::wstring p = rsxrtest::TempIni(L"config", "[color]\r\nstring6_color=\r\nstring6_color_cb=\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.colorValid && c.string6Rgb == 0xC8C8C8u && c.string6Raw.empty());
    CHECK(!c.colorCbSet && c.colorCbValid && c.string6RgbCb == 0xC8C8C8u);
    DeleteFileW(p.c_str());
}

// A value longer than the read buffer must not be cut into something valid: "102030" followed by
// blanks and junk used to be truncated to "102030<blanks>" (64-char buffer) and parse.
TEST(config_overlong_colour_is_invalid_not_truncated) {
    for (size_t pad : {60u, 400u}) {
        const std::string v = "102030" + std::string(pad, ' ') + "zz";
        const std::string ini = "[color]\r\nstring6_color=" + v + "\r\nstring6_color_cb=" + v + "\r\n";
        const std::wstring p = rsxrtest::TempIni(L"config", ini.c_str());
        const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

        CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u && !c.string6Raw.empty());
        CHECK(c.colorCbSet && !c.colorCbValid && c.string6RgbCb == 0xC8C8C8u);
        DeleteFileW(p.c_str());
    }
}

TEST(config_overlong_value_of_blanks_after_a_valid_colour_is_rejected) {
    // Exactly what truncation produced: a valid colour, then more text than the buffer holds.
    const std::string v = "#102030" + std::string(600, ' ') + "1";
    const std::string ini = "[color]\r\nstring6_color=" + v + "\r\n";
    const std::wstring p = rsxrtest::TempIni(L"config", ini.c_str());
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u);
    DeleteFileW(p.c_str());
}

TEST(config_string7_line_defaults_on_and_can_be_switched_off) {
    CHECK(RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini").string7Line);
    std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\ndiagnostics=0\r\n");
    RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.string7Line);
    CHECK_EQ(c.string7LineRaw, 1);
    p = rsxrtest::TempIni(L"config", "[neck]\r\nstring7_line=0\r\n");
    c = RsxrTest_LoadConfig(p.c_str());
    CHECK(!c.string7Line);
    CHECK_EQ(c.string7LineRaw, 0);
    DeleteFileW(p.c_str());
}

TEST(config_song_end_log_defaults_on_and_can_be_switched_off) {
    CHECK(RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini").songEndLog);
    const std::wstring p = rsxrtest::TempIni(L"config", "[debug]\r\nsong_end_log=0\r\n");

    CHECK(!RsxrTest_LoadConfig(p.c_str()).songEndLog);
    DeleteFileW(p.c_str());
}

TEST(config_missing_key_or_section_is_off) {
    const std::wstring p =
        rsxrtest::TempIni(L"config", "[other]\r\nstring6_as_string0=1\r\ndiagnostics=1\r\n[debug]\r\nfoo=1\r\n");
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(c.found);
    CHECK_EQ(c.removedKeys, 0);  // a removed key in another section is not counted
    CHECK(!c.diagnostics);
    DeleteFileW(p.c_str());
}

TEST(config_invalid_switches_fail_closed) {
    for (const std::string bad : std::vector<std::string>{"", "-1", "2", "1junk", "nan", std::string(1000, '9'),
                                                          "1" + std::string(300, ' ') + "junk"}) {
        const auto p = rsxrtest::TempIni(
            L"config", ("[neck]\r\nstring7_line=" + bad + "\r\n[debug]\r\ndiagnostics=" + bad + "\r\n").c_str());
        const auto c = RsxrTest_LoadConfig(p.c_str());

        CHECK(!c.string7Line);
        CHECK(!c.diagnostics);
        DeleteFileW(p.c_str());
    }
}

TEST(config_malformed_autotune_is_disabled) {
    for (const std::string bad : std::vector<std::string>{"-1", "30junk", std::string(1000, '9')}) {
        const auto p = rsxrtest::TempIni(L"config", ("[debug]\r\ntuner_autotune=" + bad + "\r\n").c_str());

        CHECK_EQ(RsxrTest_LoadConfig(p.c_str()).tunerAutotune, 0);
        DeleteFileW(p.c_str());
    }
}

TEST(config_relative_ini_is_read_from_the_current_directory) {
    const auto p = rsxrtest::TempIni(L"config", "[neck]\r\nstring7_line=0\r\n");
    std::vector<wchar_t> cwd(32768);  // 64 KB: on the heap, not the stack

    CHECK(GetCurrentDirectoryW(DWORD(cwd.size()), cwd.data()) != 0);
    const auto slash = p.find_last_of(L"\\/");

    CHECK(SetCurrentDirectoryW(p.substr(0, slash).c_str()));
    const auto c = RsxrTest_LoadConfig(p.substr(slash + 1).c_str());

    CHECK(SetCurrentDirectoryW(cwd.data()));
    CHECK(c.found);
    CHECK(!c.string7Line);
    DeleteFileW(p.c_str());
}

TEST(config_unicode_ini_filename_uses_wide_file_apis) {
    const auto p = rsxrtest::TempIni(L"config", "[neck]\r\nstring7_line=0\r\n");
    const auto unicode = p + L".J\u00f6rg.ini";

    CHECK(MoveFileExW(p.c_str(), unicode.c_str(), MOVEFILE_REPLACE_EXISTING));
    const auto c = RsxrTest_LoadConfig(unicode.c_str());

    CHECK(c.found && !c.string7Line);
    DeleteFileW(unicode.c_str());
}
