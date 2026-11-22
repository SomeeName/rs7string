#include "config_fixture.h"
#include "d3d/swap.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/scene/scene.h"
#include "features/songend/songend.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "features/window/window.h"
#include "hook_rules.h"
#include "platform/config.h"
#include "platform/settings.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {
std::string ReadAll(const std::wstring& path) {
    std::string s;
    FILE* f = nullptr;

    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) {
        return s;
    }

    char buf[512];
    size_t n;

    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        s.append(buf, n);
    }

    std::fclose(f);

    return s;
}

// The globals the apply functions write, back to the values Rsxr_Startup gives a missing ini.
struct Restore {
    Restore() { RsxrSet_ResetForTests(); }

    ~Restore() {
        g_rsxr_diag = 0;
        g_rsxr_lowrange = 0;
        g_rsxr_tuner_auto = 0;
        g_rsxr_endlog = 1;
        g_rsxr_tuner_any = 0;
        g_rsxr_spacing[2] = 0.0f;
        g_rsxr_match_misses = 2;

        for (float& f : g_rsxr_fx_rgb) {
            f = 0.0f;
        }

        RsxrSwap_UninstallForTests();  // also drops a pending recolour request
        RsxrSet_ResetForTests();
    }
};
}  // namespace

TEST(settings_review_rejects_malformed_input_without_changing_state) {
    Restore r;

    struct Bad {
        const char* id;
        const char* text;
    };

    const Bad cases[] = {{"debug.diagnostics", "2"},
                         {"debug.diagnostics", "true"},
                         {"debug.tuner_autotune", "31"},
                         {"debug.tuner_autotune", "1.5"},
                         {"debug.tuner_autotune", "99999999999999999999999"},
                         {"neck.spacing_guitar7", "nan"},
                         {"neck.spacing_guitar7", "inf"},
                         {"neck.spacing_guitar7", "1e999"},
                         {"neck.spacing_guitar7", "0.4suffix"},
                         {"neck.spacing_guitar7", ""},
                         {"neck.spacing_guitar7", nullptr},
                         {"chords.wide_misses", "3"},
                         {"color.string6_color", "#12345"},
                         {"tuner.name_suffix", "line\nbreak"},
                         {"tuner.name_suffix", "\xC3\xA4"},
                         {"tuner.name_suffix", "01234567890123456789012345678901"}};

    for (const Bad& b : cases) {
        const int i = RsxrSet_Find(b.id);
        const RsxrSetState before = RsxrSet_State(i);

        CHECK(RsxrSet_SetText(b.id, b.text) == RsxrSetResult::Invalid);
        const RsxrSetState after = RsxrSet_State(i);
        const auto type = RsxrSet_Def(i).type;

        CHECK(RsxrSet_Equal(type, before.value, after.value));
        CHECK(RsxrSet_Equal(type, before.running, after.running));
        CHECK(RsxrSet_Equal(type, before.saved, after.saved));
        CHECK(!RsxrSet_Dirty(i));
    }

    CHECK(RsxrSet_SetText("no.such.key", "1") == RsxrSetResult::Unknown);
}

TEST(settings_review_parses_boundaries_and_whitespace) {
    Restore r;

    CHECK(RsxrSet_SetText("debug.tuner_autotune", " +30\t") == RsxrSetResult::Live);
    CHECK_EQ(RsxrSet_Int("debug.tuner_autotune"), 30);
    CHECK(RsxrSet_SetText("neck.spacing_guitar7", " 4e0\t") == RsxrSetResult::Live);
    CHECK_EQ(RsxrSet_Float("neck.spacing_guitar7"), 4.0f);
    const std::string limit(31, 'x');

    CHECK(RsxrSet_SetText("tuner.name_suffix", limit.c_str()) == RsxrSetResult::Restart);
    CHECK_EQ(std::string(RsxrSet_Text("tuner.name_suffix")), limit);
    CHECK(RsxrSet_SetText("tuner.name_suffix", " \t") == RsxrSetResult::Restart);
    CHECK_EQ(std::string(RsxrSet_Text("tuner.name_suffix")), std::string(""));
}

// The ini read strips quotes around a value, so a quoted text would not read back after a save.
TEST(settings_text_wrapped_in_quotes_is_invalid) {
    Restore r;

    CHECK(RsxrSet_SetText("tuner.name_suffix", "\"x\"") == RsxrSetResult::Invalid);
    CHECK(RsxrSet_SetText("tuner.name_suffix", "'x'") == RsxrSetResult::Invalid);
    CHECK(RsxrSet_SetText("tuner.name_suffix", "a\"b") == RsxrSetResult::Restart);  // a quote inside is fine
}

// Floats are written with the shortest text that reads back as the same float, so a save keeps the value.
TEST(settings_float_text_reads_back_as_the_same_float) {
    Restore r;
    const int i = RsxrSet_Find("neck.spacing_guitar7");

    for (const char* text : {"1.34", "0.05", "1.23456"}) {
        CHECK(RsxrSet_SetText("neck.spacing_guitar7", text) == RsxrSetResult::Live);
        const std::string saved = RsxrSet_Format(i, RsxrSet_State(i).value);

        CHECK_EQ(std::strtof(saved.c_str(), nullptr), RsxrSet_Float("neck.spacing_guitar7"));
    }

    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "0.4") == RsxrSetResult::Live);
    CHECK_EQ(RsxrSet_Format(i, RsxrSet_State(i).value), std::string("0.4"));
}

TEST(settings_review_typed_text_respects_affix_length) {
    Restore r;
    RsxrSetValue v{};

    std::memset(v.text, 'x', 32);
    CHECK(RsxrSet_Commit(RsxrSet_Find("tuner.name_suffix"), v) == RsxrSetResult::Invalid);
}

TEST(settings_table_is_consistent) {
    Restore r;

    CHECK(RsxrSet_Count() > 20);

    for (int i = 0; i < RsxrSet_Count(); ++i) {
        const RsxrSetDef& d = RsxrSet_Def(i);
        char id[96];

        std::snprintf(id, sizeof id, "%ls.%ls", d.section, d.key);
        CHECK(std::strcmp(id, d.id) == 0);
        CHECK_EQ(RsxrSet_Find(d.id), i);
        // A Live/NextSession key must reach the game; a startup key must not pretend to.
        CHECK((d.when == RsxrSetWhen::Restart) == (d.apply == nullptr));
        CHECK(!RsxrSet_Dirty(i));
        CHECK(!RsxrSet_NeedsRestart(i));
        CHECK(RsxrSet_SetText(d.id, d.def) == RsxrSetResult::Unchanged);  // the default text parses to the default
    }

    CHECK_EQ(RsxrSet_Find("spacing_guitar7"), RsxrSet_Find("neck.spacing_guitar7"));
    CHECK_EQ(RsxrSet_Find("neck.nope"), -1);
    CHECK_EQ(RsxrSet_Find("nope"), -1);
}

TEST(settings_defaults_match_the_startup_defaults) {
    Restore r;
    const RsxrTestConfig c = RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini");

    CHECK(!RsxrSet_InitFromIni(L"Z:\\does\\not\\exist\\rsxr.ini"));  // missing file: every default
    CHECK_EQ(RsxrSet_State(RsxrSet_Find("color.string6_color")).value.rgb, c.string6Rgb);
    CHECK(RsxrSet_Bool("neck.string7_line") == c.string7Line);
    CHECK_EQ(RsxrSet_Int("chords.wide_misses"), 2);
    CHECK(RsxrSet_Bool("tuner.lowrange") == c.tunerLowRange);
    CHECK(RsxrSet_Bool("debug.song_end_log") == c.songEndLog);
    CHECK(RsxrSet_Bool("overlay.enabled"));  // on by default, but hidden until F9
    CHECK(!RsxrSet_Bool("overlay.open"));
    CHECK(std::strcmp(RsxrSet_Text("overlay.hotkey"), "F9") == 0);
}

TEST(settings_live_key_reaches_the_global_at_once) {
    Restore r;

    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "1.34") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_spacing[2], 1.34f);
    const int i = RsxrSet_Find("neck.spacing_guitar7");

    CHECK(RsxrSet_Dirty(i));
    CHECK(!RsxrSet_NeedsRestart(i));
    CHECK(RsxrSet_SetText("spacing_guitar7", "1.34") == RsxrSetResult::Unchanged);
    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "4.5") == RsxrSetResult::Invalid);  // spacings are 0..4
    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "0.5x") == RsxrSetResult::Invalid);
    CHECK_EQ(g_rsxr_spacing[2], 1.34f);
    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "0.05") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_spacing[2], 0.0f);  // below 0.1: stock spacing, as the ini read gives
    CHECK(RsxrSet_SetText("neck.spacing_guitar7", "1.34") == RsxrSetResult::Live);
    CHECK(RsxrSet_SetText("chords.wide_misses", "0") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_match_misses, uintptr_t(0));
    CHECK(RsxrSet_SetText("chords.wide_misses", "3") == RsxrSetResult::Invalid);
    CHECK(RsxrSet_SetText("debug.diagnostics", "1") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_diag, uintptr_t(1));
    CHECK(RsxrSet_SetText("debug.diagnostics", "2") == RsxrSetResult::Invalid);
}

TEST(settings_restart_key_keeps_the_running_value) {
    Restore r;

    g_rsxr_s7_on = 1;
    CHECK(RsxrSet_SetText("neck.string7_line", "0") == RsxrSetResult::Restart);
    CHECK_EQ(g_rsxr_s7_on, uintptr_t(1));  // N1-N7 were set up at startup: untouched
    const int i = RsxrSet_Find("neck.string7_line");

    CHECK(RsxrSet_NeedsRestart(i));
    CHECK(RsxrSet_Dirty(i));
    CHECK_EQ(RsxrSet_State(i).running.i, 1);
    CHECK(RsxrSet_SetText("neck.string7_line", "1") == RsxrSetResult::Restart);
    CHECK(!RsxrSet_NeedsRestart(i));
    CHECK(!RsxrSet_Dirty(i));
}

TEST(settings_colour_sets_the_rgb_table_per_mode) {
    Restore r;

    CHECK(RsxrSet_SetText("color.string6_color", "#FF0080") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_fx_rgb[0], 1.0f);
    CHECK_EQ(g_rsxr_fx_rgb[1], 0.0f);
    CHECK_EQ(g_rsxr_fx_rgb[2], 128.0f / 255.0f);
    CHECK_EQ(g_rsxr_fx_rgb[3], 200.0f / 255.0f);  // colourblind: still the default C8C8C8
    CHECK(RsxrSet_SetText("color.string6_color_cb", "00ff00") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_fx_rgb[3], 0.0f);
    CHECK_EQ(g_rsxr_fx_rgb[4], 1.0f);
    CHECK(RsxrSet_SetText("color.string6_color", "GG0000") == RsxrSetResult::Invalid);
    CHECK(RsxrSet_SetText("color.string6_color", "FF00") == RsxrSetResult::Invalid);
}

TEST(settings_removed_string6_as_string0_is_not_a_setting) {
    Restore r;

    CHECK(RsxrSet_SetText("debug.string6_as_string0", "1") != RsxrSetResult::Live);
}

TEST(settings_tuner_keys) {
    Restore r;

    g_rsxr_tuner_any = 0;
    CHECK(RsxrSet_SetText("debug.tuner_autotune", "5") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_tuner_auto, uintptr_t(5));
    CHECK_EQ(g_rsxr_tuner_any, 1L);  // the tuner stubs enter C++ from now on
    CHECK(RsxrSet_SetText("debug.tuner_autotune", "31") == RsxrSetResult::Invalid);
    CHECK(RsxrSet_Bool("tuner.lowrange"));
    CHECK(RsxrSet_SetText("tuner.lowrange", "0") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_lowrange, uintptr_t(0));
    CHECK(RsxrSet_SetText("tuner.lowrange", "1") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_lowrange, uintptr_t(1));
    // 1.0: [tuner] seven_slot and [debug] tuner_trace were removed with the B-phase tuner.
    CHECK(RsxrSet_Find("tuner.seven_slot") < 0);
    CHECK(RsxrSet_Find("debug.tuner_trace") < 0);
    CHECK(RsxrSet_SetText("debug.song_end_log", "0") == RsxrSetResult::Live);
    CHECK_EQ(g_rsxr_endlog, uintptr_t(0));
}

TEST(settings_headstock_values_go_to_the_live_override) {
    Restore r;

    CHECK(RsxrSet_Find("neck.peg7_offset") < 0 && RsxrSet_Find("neck.peg7_k") < 0);  // 1.0: the stage-2 peg's keys
    CHECK(RsxrSet_SetText("debug.head_cull", "  hardware_tuning:1 ") == RsxrSetResult::Live);
    CHECK(std::strcmp(RsxrSet_Text("debug.head_cull"), "hardware_tuning:1") == 0);
    RsxrHeadStats hs{};

    RsxrHead_Stats(&hs);
    CHECK(hs.liveOwned);  // the ini is no longer re-read: it would undo the overlay's values
}

TEST(settings_save_writes_only_changed_keys_and_keeps_the_rest) {
    Restore r;
    const std::wstring p =
        rsxrtest::TempIni(L"settings", "; my notes\r\n[neck]\r\nspacing_guitar7=0\r\n[other]\r\nfoo=bar\r\n");

    CHECK(RsxrSet_InitFromIni(p.c_str()));
    CHECK_EQ(RsxrSet_DirtyCount(), 0);
    CHECK(RsxrSet_State(RsxrSet_Find("neck.spacing_guitar7")).fromIni);
    CHECK(!RsxrSet_State(RsxrSet_Find("neck.spacing_guitar8")).fromIni);
    RsxrSet_SetText("neck.spacing_guitar7", "0.6");
    RsxrSet_SetText("color.string6_color", "112233");
    RsxrSet_SetText("window.screen", "2");
    RsxrSet_SetText("assets.headstock7_wood", "0.75");
    CHECK_EQ(RsxrSet_DirtyCount(), 4);
    CHECK_EQ(RsxrSet_Save(p.c_str()), 4);
    CHECK_EQ(RsxrSet_DirtyCount(), 0);
    CHECK_EQ(RsxrSet_Save(p.c_str()), 0);
    const RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK_EQ(c.string6Rgb, 0x112233u);
    CHECK_EQ(GetPrivateProfileIntW(L"window", L"screen", 0, p.c_str()), 2u);
    wchar_t wood[64] = {};

    GetPrivateProfileStringW(L"assets", L"headstock7_wood", L"", wood, 64, p.c_str());
    CHECK(std::wcscmp(wood, L"0.75") == 0);
    const std::string all = ReadAll(p);

    CHECK(all.find("; my notes") != std::string::npos);
    CHECK(all.find("foo=bar") != std::string::npos);
    CHECK(all.find("spacing_guitar8") == std::string::npos);  // unchanged keys are not written
    // A restart key that was saved is still "restart required" until the game restarts.
    CHECK(RsxrSet_NeedsRestart(RsxrSet_Find("window.screen")));
    RsxrSet_InitFromIni(p.c_str());
    CHECK_EQ(RsxrSet_Float("neck.spacing_guitar7"), 0.6f);
    CHECK_EQ(RsxrSet_Int("window.screen"), 2);
    CHECK(!RsxrSet_NeedsRestart(RsxrSet_Find("window.screen")));
    DeleteFileW(p.c_str());
}

TEST(settings_save_resolves_a_bare_filename_like_the_reader) {
    Restore r;
    const std::wstring p = rsxrtest::TempIni(L"relative_save", "[debug]\r\ndiagnostics=0\r\n");
    std::vector<wchar_t> previous(32768);

    CHECK(GetCurrentDirectoryW(DWORD(previous.size()), previous.data()) != 0);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring directory = p.substr(0, slash);
    const std::wstring filename = p.substr(slash + 1);

    CHECK(SetCurrentDirectoryW(directory.c_str()));
    CHECK(RsxrSet_InitFromIni(filename.c_str()));
    CHECK(RsxrSet_SetText("debug.diagnostics", "1") == RsxrSetResult::Live);
    CHECK_EQ(RsxrSet_Save(filename.c_str()), 1);
    CHECK_EQ(GetPrivateProfileIntW(L"debug", L"diagnostics", 0, p.c_str()), 1u);
    CHECK_EQ(RsxrSet_DirtyCount(), 0);
    CHECK(SetCurrentDirectoryW(previous.data()));
    DeleteFileW(p.c_str());
}

TEST(settings_save_rejects_an_empty_path_without_marking_values_saved) {
    Restore r;

    RsxrSet_SetText("debug.diagnostics", "1");
    CHECK_EQ(RsxrSet_Save(L""), -1);
    CHECK_EQ(RsxrSet_Save(nullptr), -1);
    CHECK_EQ(RsxrSet_DirtyCount(), 1);
}

TEST(settings_invalid_ini_numbers_disable_the_setting) {
    Restore r;
    const std::wstring p = rsxrtest::TempIni(
        L"settings", "[neck]\r\nspacing_guitar7=junk\r\nstring7_line=5\r\n[debug]\r\ntuner_autotune=99\r\n"
                     "[color]\r\nstring6_color=nothex\r\n");

    RsxrSet_InitFromIni(p.c_str());
    CHECK_EQ(RsxrSet_Float("neck.spacing_guitar7"), 0.0f);
    CHECK(!RsxrSet_Bool("neck.string7_line"));
    CHECK_EQ(RsxrSet_Int("debug.tuner_autotune"), 0);
    CHECK_EQ(RsxrSet_State(RsxrSet_Find("color.string6_color")).value.rgb, 0xC8C8C8u);
    DeleteFileW(p.c_str());
}

TEST(settings_read_table_int_uses_the_row_default_and_range) {
    const std::wstring p = rsxrtest::TempIni(L"settings", "[window]\r\nscreen=3\r\n[debug]\r\ntuner_autotune=31\r\n");

    CHECK_EQ(RsxrSet_ReadTableInt(kRsxrWindowSettings, "window.screen", p.c_str()), 3);
    CHECK_EQ(RsxrSet_ReadTableInt(kRsxrWindowSettings, "window.background", p.c_str()), 0);    // missing: default "0"
    CHECK_EQ(RsxrSet_ReadTableInt(kRsxrTunerSettings, "tuner.lowrange", p.c_str()), 1);        // missing: default "1"
    CHECK_EQ(RsxrSet_ReadTableInt(kRsxrTunerSettings, "debug.tuner_autotune", p.c_str()), 0);  // above hi 30
    CHECK_EQ(RsxrSet_ReadTableInt(kRsxrTunerSettings, "window.screen", p.c_str()), 0);         // not in this table
    DeleteFileW(p.c_str());
}

TEST(settings_numeric_prefixes_and_overflow_fail_closed) {
    Restore r;

    // The last one is cut by the ini read to "1" and blanks, which alone would parse as 1.
    for (const std::string bad :
         std::vector<std::string>{"-1", "1junk", std::string(1000, '9'), "1" + std::string(300, ' ') + "junk"}) {
        const auto p = rsxrtest::TempIni(
            L"settings", ("[neck]\r\nnative_pitch=" + bad + "\r\n[window]\r\nscreen=" + bad + "\r\n").c_str());

        RsxrSet_InitFromIni(p.c_str());
        CHECK(!RsxrSet_Bool("neck.native_pitch"));
        CHECK_EQ(RsxrSet_Int("window.screen"), 0);
        DeleteFileW(p.c_str());
    }
}
