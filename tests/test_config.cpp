#include "core/config.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {
std::wstring TempIni(const char* content) {
    wchar_t dir[MAX_PATH];

    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"rs7string_test_config.ini";
    FILE* f = nullptr;

    _wfopen_s(&f, path.c_str(), L"wb");

    if (f) {
        std::fputs(content, f);
        std::fclose(f);
    }

    return path;
}
}  // namespace

TEST(config_missing_file_uses_the_native_defaults) {
    const Rs7Config c = Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini");

    CHECK(!c.found);
    CHECK(c.nativeChordloops);
    CHECK_EQ(c.nativeChordloopsRaw, 1);
    CHECK(!c.eight);
    CHECK(!c.tunerLowRange);
    CHECK_EQ(c.tunerLowRangeRaw, 0);
    CHECK_EQ(c.removedKeys, 0);
    CHECK(!c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 0);
}

TEST(config_removed_keys_are_ignored_and_counted) {
    // 1.0: an old ini with a removed legacy key still loads; the key changes nothing and is only logged.
    const std::wstring p = TempIni("[debug]\r\nstring6_as_string0=1\r\ndiagnostics=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.found);
    CHECK_EQ(c.removedKeys, 1);
    CHECK(c.diagnostics);
    DeleteFileW(p.c_str());
}

TEST(config_removed_keys_table_lists_each_key_once) {
    for (int i = 0; i < kRs7RemovedKeyCount; ++i) {
        for (int j = i + 1; j < kRs7RemovedKeyCount; ++j) {
            CHECK(std::wcscmp(kRs7RemovedKeys[i].section, kRs7RemovedKeys[j].section) ||
                  std::wcscmp(kRs7RemovedKeys[i].key, kRs7RemovedKeys[j].key));
        }
    }
}

TEST(config_diagnostics_default_off) {
    const std::wstring p = TempIni("[debug]\r\nstring6_as_string0=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.found);
    CHECK(!c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 0);
    DeleteFileW(p.c_str());
}

TEST(config_diagnostics_on) {
    const std::wstring p = TempIni("[debug]\r\ndiagnostics=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.found);
    CHECK(c.diagnostics);
    CHECK_EQ(c.diagnosticsRaw, 1);
    DeleteFileW(p.c_str());
}

TEST(config_diagnostics_off_explicitly) {
    const std::wstring p = TempIni("[debug]\r\ndiagnostics=0\r\nstring6_as_string0=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(!c.diagnostics);
    CHECK_EQ(c.removedKeys, 1);
    DeleteFileW(p.c_str());
}

TEST(config_color_defaults) {
    const Rs7Config c = Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini");

    CHECK_EQ(c.string6Rgb, 0xC8C8C8u);
    CHECK_EQ(c.string6RgbCb, 0xC8C8C8u);
    CHECK(c.rampSwap && !c.allowInexact && c.colorValid && !c.colorCbSet);
    CHECK_EQ(c.rampSwapRaw, 1);
    CHECK_EQ(c.allowInexactRaw, 0);
}

TEST(config_color_cb_defaults_to_the_normal_colour) {
    const std::wstring p = TempIni("[color]\r\nstring6_color=FF00FF\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK_EQ(c.string6Rgb, 0xFF00FFu);
    CHECK_EQ(c.string6RgbCb, 0xFF00FFu);
    CHECK(c.colorValid && !c.colorCbSet && c.colorCbValid && c.string6Raw == L"FF00FF");
    CHECK(c.rampSwap && !c.allowInexact);
    DeleteFileW(p.c_str());
}

TEST(config_color_cb_and_switches) {
    const std::wstring p =
        TempIni("[color]\r\nstring6_color=FF00FF\r\nstring6_color_cb=00FFFF\r\nramp_swap=0\r\nallow_inexact=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK_EQ(c.string6RgbCb, 0x00FFFFu);
    CHECK(c.colorCbSet && c.colorCbValid && !c.rampSwap && c.allowInexact);
    CHECK(c.rampSwapRaw == 0 && c.allowInexactRaw == 1);
    DeleteFileW(p.c_str());
}

TEST(config_invalid_colour_falls_back_to_default_and_keeps_the_raw_text) {
    const std::wstring p = TempIni("[color]\r\nstring6_color=red\r\nstring6_color_cb=12345\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u && c.string6Raw == L"red");
    CHECK(!c.colorCbValid && c.string6RgbCb == 0xC8C8C8u);  // invalid cb -> the (valid or default) normal colour
    CHECK(c.colorCbSet && c.string6CbRaw == L"12345");
    DeleteFileW(p.c_str());
}

TEST(config_invalid_cb_uses_the_valid_normal_colour) {
    const std::wstring p = TempIni("[color]\r\nstring6_color=#102030\r\nstring6_color_cb=nope\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.colorValid && c.string6Rgb == 0x102030u);
    CHECK(c.colorCbSet && !c.colorCbValid && c.string6RgbCb == 0x102030u);
    DeleteFileW(p.c_str());
}

TEST(config_empty_colour_keeps_the_default_as_valid) {
    const std::wstring p = TempIni("[color]\r\nstring6_color=\r\nstring6_color_cb=\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

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
        const std::wstring p = TempIni(ini.c_str());
        const Rs7Config c = Rs7Config_Load(p.c_str());

        CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u && !c.string6Raw.empty());
        CHECK(c.colorCbSet && !c.colorCbValid && c.string6RgbCb == 0xC8C8C8u);
        DeleteFileW(p.c_str());
    }
}

TEST(config_overlong_value_of_blanks_after_a_valid_colour_is_rejected) {
    // Exactly what truncation produced: a valid colour, then more text than the buffer holds.
    const std::string v = "#102030" + std::string(600, ' ') + "1";
    const std::string ini = "[color]\r\nstring6_color=" + v + "\r\n";
    const std::wstring p = TempIni(ini.c_str());
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(!c.colorValid && c.string6Rgb == 0xC8C8C8u);
    DeleteFileW(p.c_str());
}

TEST(config_string7_line_defaults_on_and_can_be_switched_off) {
    CHECK(Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini").string7Line);
    std::wstring p = TempIni("[debug]\r\ndiagnostics=0\r\n");
    Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.string7Line);
    CHECK_EQ(c.string7LineRaw, 1);
    p = TempIni("[neck]\r\nstring7_line=0\r\n");
    c = Rs7Config_Load(p.c_str());
    CHECK(!c.string7Line);
    CHECK_EQ(c.string7LineRaw, 0);
    DeleteFileW(p.c_str());
}

TEST(config_string7_v_defaults) {
    Rs7Config c = Rs7Config_Load(L"Z:\does\not\exist\rs7string.ini");

    CHECK(c.string7VValid);
    CHECK(c.string7V == 0.4f);
    const std::wstring p = TempIni("[neck]\r\nstring7_line=1\r\n");

    c = Rs7Config_Load(p.c_str());
    CHECK(c.string7VValid && c.string7V == 0.4f && c.string7VRaw.empty());
    DeleteFileW(p.c_str());
}

TEST(config_string7_v_set) {
    const std::wstring p = TempIni("[neck]\r\nstring7_v=0.3\r\nstring7_draw_fix=0\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.string7VValid);
    CHECK(c.string7V == 0.3f);
    CHECK_EQ(c.removedKeys, 1);  // string7_draw_fix: removed in 1.0 with the S7 object
    DeleteFileW(p.c_str());
}

TEST(config_string7_v_invalid_keeps_the_default_and_the_text) {
    for (const char* bad :
         {"[neck]\r\nstring7_v=abc\r\n", "[neck]\r\nstring7_v=0.95\r\n", "[neck]\r\nstring7_v=-0.1\r\n",
          "[neck]\r\nstring7_v=0.3x\r\n", "[neck]\r\nstring7_v=nan\r\n"}) {
        const std::wstring p = TempIni(bad);
        const Rs7Config c = Rs7Config_Load(p.c_str());

        CHECK(!c.string7VValid);
        CHECK(c.string7V == 0.4f);
        CHECK(!c.string7VRaw.empty());
        DeleteFileW(p.c_str());
    }
}

TEST(config_song_end_log_defaults_on_and_can_be_switched_off) {
    CHECK(Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini").songEndLog);
    const std::wstring p = TempIni("[debug]\r\nsong_end_log=0\r\n");

    CHECK(!Rs7Config_Load(p.c_str()).songEndLog);
    DeleteFileW(p.c_str());
}

TEST(parse_atlas_row) {
    float v = -1.0f;

    CHECK(Rs7_ParseAtlasRow(L"0", &v) && v == 0.0f);
    CHECK(Rs7_ParseAtlasRow(L" 0.9 ", &v) && v == 0.9f);
    CHECK(Rs7_ParseAtlasRow(L"0.25", &v) && v == 0.25f);
    CHECK(!Rs7_ParseAtlasRow(L"", &v));
    CHECK(!Rs7_ParseAtlasRow(L"  ", &v));
    CHECK(!Rs7_ParseAtlasRow(L"1", &v));
    CHECK(!Rs7_ParseAtlasRow(L"inf", &v));
    CHECK(!Rs7_ParseAtlasRow(nullptr, &v));
}

TEST(config_missing_key_or_section_is_off) {
    const std::wstring p = TempIni("[other]\r\nstring6_as_string0=1\r\ndiagnostics=1\r\n[debug]\r\nfoo=1\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(c.found);
    CHECK_EQ(c.removedKeys, 0);  // a removed key in another section is not counted
    CHECK(!c.diagnostics);
    DeleteFileW(p.c_str());
}

TEST(config_fret_extend_defaults_on_with_the_estimated_half_length) {
    Rs7Config c = Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini");

    CHECK(c.fretExtend && c.fretHalfLenValid && c.fretHalfLen == 4.61f);
    const std::wstring p = TempIni("[neck]\r\nfret_extend=0\r\nfret_halflen=4.25\r\n");

    c = Rs7Config_Load(p.c_str());
    CHECK(!c.fretExtend);
    CHECK_EQ(c.fretExtendRaw, 0);
    CHECK(c.fretHalfLenValid && c.fretHalfLen == 4.25f);
    DeleteFileW(p.c_str());
}

TEST(config_fret_halflen_invalid_keeps_the_default_and_the_text) {
    for (const char* bad : {"[neck]\r\nfret_halflen=abc\r\n", "[neck]\r\nfret_halflen=0.5\r\n",
                            "[neck]\r\nfret_halflen=25\r\n", "[neck]\r\nfret_halflen=4.6x\r\n"}) {
        const std::wstring p = TempIni(bad);
        const Rs7Config c = Rs7Config_Load(p.c_str());

        CHECK(!c.fretHalfLenValid);
        CHECK(c.fretHalfLen == 4.61f);
        CHECK(!c.fretHalfLenRaw.empty());
        DeleteFileW(p.c_str());
    }
}

TEST(config_headstock_peg_defaults_on_and_can_be_switched_off) {
    CHECK(Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini").headstockPeg);
    const std::wstring e = TempIni("[neck]\r\nstring7_line=1\r\n");

    CHECK(Rs7Config_Load(e.c_str()).headstockPeg);
    DeleteFileW(e.c_str());
    const std::wstring p = TempIni("[neck]\r\nheadstock_peg=0\r\n");
    const Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(!c.headstockPeg);
    CHECK_EQ(c.headstockPegRaw, 0);
    DeleteFileW(p.c_str());
}

TEST(config_eight_default_off_and_optin) {
    CHECK(!Rs7Config_Load(L"Z:\\missing.ini").eight);
    const auto p = TempIni("[neck]\r\neight=1\r\n");

    CHECK(Rs7Config_Load(p.c_str()).eight);
    DeleteFileW(p.c_str());
}

TEST(config_invalid_switches_fail_closed) {
    for (const std::string bad : std::vector<std::string>{"", "-1", "2", "1junk", "nan", std::string(1000, '9'),
                                                          "1" + std::string(300, ' ') + "junk"}) {
        const auto p =
            TempIni(("[neck]\r\nfret_extend=" + bad + "\r\neight=" + bad + "\r\n[debug]\r\ndiagnostics=" + bad + "\r\n")
                        .c_str());
        const auto c = Rs7Config_Load(p.c_str());

        CHECK(!c.fretExtend);
        CHECK(!c.eight);
        CHECK(!c.diagnostics);
        DeleteFileW(p.c_str());
    }
}

TEST(config_malformed_autotune_is_disabled) {
    for (const std::string bad : std::vector<std::string>{"-1", "30junk", std::string(1000, '9')}) {
        const auto p = TempIni(("[debug]\r\ntuner_autotune=" + bad + "\r\n").c_str());

        CHECK_EQ(Rs7Config_Load(p.c_str()).tunerAutotune, 0);
        DeleteFileW(p.c_str());
    }
}

TEST(config_relative_ini_is_read_from_the_current_directory) {
    const auto p = TempIni("[neck]\r\nfret_extend=0\r\n");
    wchar_t cwd[32768]{};

    CHECK(GetCurrentDirectoryW(32768, cwd) != 0);
    const auto slash = p.find_last_of(L"\\/");

    CHECK(SetCurrentDirectoryW(p.substr(0, slash).c_str()));
    const auto c = Rs7Config_Load(p.substr(slash + 1).c_str());

    CHECK(SetCurrentDirectoryW(cwd));
    CHECK(c.found);
    CHECK(!c.fretExtend);
    DeleteFileW(p.c_str());
}

TEST(config_unicode_ini_filename_uses_wide_file_apis) {
    const auto p = TempIni("[neck]\r\nfret_extend=0\r\n");
    const auto unicode = p + L".J\u00f6rg.ini";

    CHECK(MoveFileExW(p.c_str(), unicode.c_str(), MOVEFILE_REPLACE_EXISTING));
    const auto c = Rs7Config_Load(unicode.c_str());

    CHECK(c.found && !c.fretExtend);
    DeleteFileW(unicode.c_str());
}
