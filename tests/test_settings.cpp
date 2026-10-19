#include "config_fixture.h"
#include "d3d/swap.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/scene/scene.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "hook_rules.h"
#include "platform/settings.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

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

// The globals the apply functions write, back to the values Rs7_Startup gives a missing ini.
struct Restore {
    Restore() { Rs7Set_ResetForTests(); }

    ~Restore() {
        g_rs7_diag = 0;
        g_rs7_lowrange = 0;
        g_rs7_tuner_auto = 0;
        g_rs7_endlog = 1;
        g_rs7_tuner_any = 0;
        g_rs7_s7_v = 0.4f;
        g_rs7_frets_on = 1;
        g_rs7_fret_halflen = 4.61f;

        for (float& f : g_rs7_fx_rgb) {
            f = 0.0f;
        }

        Rs7Swap_UninstallForTests();  // also drops a pending recolour request
        Rs7Set_ResetForTests();
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
                         {"neck.string7_v", "nan"},
                         {"neck.string7_v", "inf"},
                         {"neck.string7_v", "1e999"},
                         {"neck.string7_v", "0.4suffix"},
                         {"neck.string7_v", ""},
                         {"neck.string7_v", nullptr},
                         {"color.string6_color", "#12345"},
                         {"tuner.name_suffix", "line\nbreak"},
                         {"tuner.name_suffix", "\xC3\xA4"},
                         {"tuner.name_suffix", "01234567890123456789012345678901"}};

    for (const Bad& b : cases) {
        const int i = Rs7Set_Find(b.id);
        const Rs7SetState before = Rs7Set_State(i);

        CHECK(Rs7Set_SetText(b.id, b.text) == Rs7SetResult::Invalid);
        const Rs7SetState after = Rs7Set_State(i);
        const auto type = Rs7Set_Def(i).type;

        CHECK(Rs7Set_Equal(type, before.value, after.value));
        CHECK(Rs7Set_Equal(type, before.running, after.running));
        CHECK(Rs7Set_Equal(type, before.saved, after.saved));
        CHECK(!Rs7Set_Dirty(i));
    }

    CHECK(Rs7Set_SetText("no.such.key", "1") == Rs7SetResult::Unknown);
}

TEST(settings_review_parses_boundaries_and_whitespace) {
    Restore r;

    CHECK(Rs7Set_SetText("debug.tuner_autotune", " +30\t") == Rs7SetResult::Live);
    CHECK_EQ(Rs7Set_Int("debug.tuner_autotune"), 30);
    CHECK(Rs7Set_SetText("neck.string7_v", " 9e-1\t") == Rs7SetResult::Live);
    CHECK_EQ(Rs7Set_Float("neck.string7_v"), 0.9f);
    const std::string limit(31, 'x');

    CHECK(Rs7Set_SetText("tuner.name_suffix", limit.c_str()) == Rs7SetResult::Restart);
    CHECK_EQ(std::string(Rs7Set_Text("tuner.name_suffix")), limit);
    CHECK(Rs7Set_SetText("tuner.name_suffix", " \t") == Rs7SetResult::Restart);
    CHECK_EQ(std::string(Rs7Set_Text("tuner.name_suffix")), std::string(""));
}

TEST(settings_review_typed_text_respects_affix_length) {
    Restore r;
    Rs7SetValue v{};

    std::memset(v.text, 'x', 32);
    CHECK(Rs7Set_Commit(Rs7Set_Find("tuner.name_suffix"), v) == Rs7SetResult::Invalid);
}

TEST(settings_table_is_consistent) {
    Restore r;

    CHECK(Rs7Set_Count() > 20);

    for (int i = 0; i < Rs7Set_Count(); ++i) {
        const Rs7SetDef& d = Rs7Set_Def(i);
        char id[96];

        std::snprintf(id, sizeof id, "%ls.%ls", d.section, d.key);
        CHECK(std::strcmp(id, d.id) == 0);
        CHECK_EQ(Rs7Set_Find(d.id), i);
        // A Live/NextSession key must reach the game; a startup key must not pretend to.
        CHECK((d.when == Rs7SetWhen::Restart) == (d.apply == nullptr));
        CHECK(!Rs7Set_Dirty(i));
        CHECK(!Rs7Set_NeedsRestart(i));
        CHECK(Rs7Set_SetText(d.id, d.def) == Rs7SetResult::Unchanged);  // the default text parses to the default
    }

    CHECK_EQ(Rs7Set_Find("string7_v"), Rs7Set_Find("neck.string7_v"));
    CHECK_EQ(Rs7Set_Find("neck.nope"), -1);
    CHECK_EQ(Rs7Set_Find("nope"), -1);
}

TEST(settings_defaults_match_the_startup_defaults) {
    Restore r;
    const Rs7TestConfig c = Rs7Test_LoadConfig(L"Z:\\does\\not\\exist\\rs7string.ini");

    Rs7Set_InitFromIni(L"Z:\\does\\not\\exist\\rs7string.ini");
    CHECK_EQ(Rs7Set_State(Rs7Set_Find("color.string6_color")).value.rgb, c.string6Rgb);
    CHECK(Rs7Set_Bool("color.ramp_swap") == c.rampSwap);
    CHECK(Rs7Set_Bool("neck.string7_line") == c.string7Line);
    CHECK(Rs7Set_Bool("neck.headstock_peg") == c.headstockPeg);
    CHECK(Rs7Set_Bool("neck.fret_extend") == c.fretExtend);
    CHECK_EQ(Rs7Set_Float("neck.string7_v"), c.string7V);
    CHECK_EQ(Rs7Set_Float("neck.fret_halflen"), c.fretHalfLen);
    CHECK(Rs7Set_Bool("tuner.lowrange") == c.tunerLowRange);
    CHECK(Rs7Set_Bool("chords.native_chordloops") == c.nativeChordloops);
    CHECK(c.nativeChordloops);  // the native default
    CHECK(Rs7Set_Bool("debug.song_end_log") == c.songEndLog);
    CHECK(Rs7Set_Bool("overlay.enabled"));  // on by default, but hidden until F9
    CHECK(!Rs7Set_Bool("overlay.open"));
    CHECK(std::strcmp(Rs7Set_Text("overlay.hotkey"), "F9") == 0);
}

TEST(settings_live_key_reaches_the_global_at_once) {
    Restore r;

    CHECK(Rs7Set_SetText("neck.string7_v", "0.55") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_s7_v, 0.55f);
    const int i = Rs7Set_Find("neck.string7_v");

    CHECK(Rs7Set_Dirty(i));
    CHECK(!Rs7Set_NeedsRestart(i));
    CHECK(Rs7Set_SetText("string7_v", "0.55") == Rs7SetResult::Unchanged);
    CHECK(Rs7Set_SetText("neck.string7_v", "0.95") == Rs7SetResult::Invalid);  // atlas rows are 0..0.9
    CHECK(Rs7Set_SetText("neck.string7_v", "0.5x") == Rs7SetResult::Invalid);
    CHECK_EQ(g_rs7_s7_v, 0.55f);
    CHECK(Rs7Set_SetText("neck.fret_extend", "0") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_frets_on, uintptr_t(0));
    CHECK(Rs7Set_SetText("neck.fret_halflen", "5.5") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_fret_halflen, 5.5f);
    CHECK(Rs7Set_SetText("neck.fret_halflen", "0.5") == Rs7SetResult::Invalid);
    CHECK(Rs7Set_SetText("debug.diagnostics", "1") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_diag, uintptr_t(1));
    CHECK(Rs7Set_SetText("debug.diagnostics", "2") == Rs7SetResult::Invalid);
}

TEST(settings_restart_key_keeps_the_running_value) {
    Restore r;

    g_rs7_s7_on = 1;
    CHECK(Rs7Set_SetText("neck.string7_line", "0") == Rs7SetResult::Restart);
    CHECK_EQ(g_rs7_s7_on, uintptr_t(1));  // N1-N7 were set up at startup: untouched
    const int i = Rs7Set_Find("neck.string7_line");

    CHECK(Rs7Set_NeedsRestart(i));
    CHECK(Rs7Set_Dirty(i));
    CHECK_EQ(Rs7Set_State(i).running.i, 1);
    CHECK(Rs7Set_SetText("neck.string7_line", "1") == Rs7SetResult::Restart);
    CHECK(!Rs7Set_NeedsRestart(i));
    CHECK(!Rs7Set_Dirty(i));
}

TEST(settings_colour_sets_the_rgb_table_per_mode) {
    Restore r;

    CHECK(Rs7Set_SetText("color.string6_color", "#FF0080") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_fx_rgb[0], 1.0f);
    CHECK_EQ(g_rs7_fx_rgb[1], 0.0f);
    CHECK_EQ(g_rs7_fx_rgb[2], 128.0f / 255.0f);
    CHECK_EQ(g_rs7_fx_rgb[3], 200.0f / 255.0f);  // colourblind: still the default C8C8C8
    CHECK(Rs7Set_SetText("color.string6_color_cb", "00ff00") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_fx_rgb[3], 0.0f);
    CHECK_EQ(g_rs7_fx_rgb[4], 1.0f);
    CHECK(Rs7Set_SetText("color.string6_color", "GG0000") == Rs7SetResult::Invalid);
    CHECK(Rs7Set_SetText("color.string6_color", "FF00") == Rs7SetResult::Invalid);
}

TEST(settings_removed_string6_as_string0_is_not_a_setting) {
    Restore r;

    CHECK(Rs7Set_SetText("debug.string6_as_string0", "1") != Rs7SetResult::Live);
}

TEST(settings_tuner_keys) {
    Restore r;

    g_rs7_tuner_any = 0;
    CHECK(Rs7Set_SetText("debug.tuner_autotune", "5") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_tuner_auto, uintptr_t(5));
    CHECK_EQ(g_rs7_tuner_any, 1L);  // the tuner stubs enter C++ from now on
    CHECK(Rs7Set_SetText("debug.tuner_autotune", "31") == Rs7SetResult::Invalid);
    CHECK(Rs7Set_Bool("tuner.lowrange"));
    CHECK(Rs7Set_SetText("tuner.lowrange", "0") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_lowrange, uintptr_t(0));
    CHECK(Rs7Set_SetText("tuner.lowrange", "1") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_lowrange, uintptr_t(1));
    // 1.0: [tuner] seven_slot and [debug] tuner_trace were removed with the B-phase tuner.
    CHECK(Rs7Set_Find("tuner.seven_slot") < 0);
    CHECK(Rs7Set_Find("debug.tuner_trace") < 0);
    CHECK(Rs7Set_SetText("debug.song_end_log", "0") == Rs7SetResult::Live);
    CHECK_EQ(g_rs7_endlog, uintptr_t(0));
}

TEST(settings_headstock_values_go_to_the_live_override) {
    Restore r;

    CHECK(Rs7Set_Find("neck.peg7_offset") < 0 && Rs7Set_Find("neck.peg7_k") < 0);  // 1.0: the stage-2 peg's keys
    CHECK(Rs7Set_SetText("debug.head_cull", "  hardware_tuning:1 ") == Rs7SetResult::Live);
    CHECK(std::strcmp(Rs7Set_Text("debug.head_cull"), "hardware_tuning:1") == 0);
    Rs7HeadStats hs{};

    Rs7Head_Stats(&hs);
    CHECK(hs.liveOwned);  // the ini is no longer re-read: it would undo the overlay's values
}

TEST(settings_save_writes_only_changed_keys_and_keeps_the_rest) {
    Restore r;
    const std::wstring p =
        rs7test::TempIni(L"settings", "; my notes\r\n[neck]\r\nstring7_v=0.4\r\n[other]\r\nfoo=bar\r\n");

    Rs7Set_InitFromIni(p.c_str());
    CHECK_EQ(Rs7Set_DirtyCount(), 0);
    CHECK(Rs7Set_State(Rs7Set_Find("neck.string7_v")).fromIni);
    CHECK(!Rs7Set_State(Rs7Set_Find("neck.fret_halflen")).fromIni);
    Rs7Set_SetText("neck.string7_v", "0.6");
    Rs7Set_SetText("color.string6_color", "112233");
    Rs7Set_SetText("window.screen", "2");
    Rs7Set_SetText("debug.head_stretch", "1.25");
    CHECK_EQ(Rs7Set_DirtyCount(), 4);
    CHECK_EQ(Rs7Set_Save(p.c_str()), 4);
    CHECK_EQ(Rs7Set_DirtyCount(), 0);
    CHECK_EQ(Rs7Set_Save(p.c_str()), 0);
    const Rs7TestConfig c = Rs7Test_LoadConfig(p.c_str());

    CHECK_EQ(c.string7V, 0.6f);
    CHECK_EQ(c.string6Rgb, 0x112233u);
    CHECK_EQ(GetPrivateProfileIntW(L"window", L"screen", 0, p.c_str()), 2u);
    wchar_t stretch[64] = {};

    GetPrivateProfileStringW(L"debug", L"head_stretch", L"", stretch, 64, p.c_str());
    CHECK(std::wcscmp(stretch, L"1.25") == 0);
    const std::string all = ReadAll(p);

    CHECK(all.find("; my notes") != std::string::npos);
    CHECK(all.find("foo=bar") != std::string::npos);
    CHECK(all.find("fret_halflen") == std::string::npos);  // unchanged keys are not written
    // A restart key that was saved is still "restart required" until the game restarts.
    CHECK(Rs7Set_NeedsRestart(Rs7Set_Find("window.screen")));
    Rs7Set_InitFromIni(p.c_str());
    CHECK_EQ(Rs7Set_Float("neck.string7_v"), 0.6f);
    CHECK_EQ(Rs7Set_Int("window.screen"), 2);
    CHECK(!Rs7Set_NeedsRestart(Rs7Set_Find("window.screen")));
    DeleteFileW(p.c_str());
}

TEST(settings_invalid_ini_numbers_disable_the_setting) {
    Restore r;
    const std::wstring p =
        rs7test::TempIni(L"settings", "[neck]\r\nstring7_v=junk\r\nfret_extend=5\r\n[debug]\r\ntuner_autotune=99\r\n"
                                      "[color]\r\nstring6_color=nothex\r\n");

    Rs7Set_InitFromIni(p.c_str());
    CHECK_EQ(Rs7Set_Float("neck.string7_v"), 0.4f);
    CHECK(!Rs7Set_Bool("neck.fret_extend"));
    CHECK_EQ(Rs7Set_Int("debug.tuner_autotune"), 0);
    CHECK_EQ(Rs7Set_State(Rs7Set_Find("color.string6_color")).value.rgb, 0xC8C8C8u);
    DeleteFileW(p.c_str());
}

TEST(settings_numeric_prefixes_and_overflow_fail_closed) {
    Restore r;

    for (const std::string bad : std::vector<std::string>{"-1", "1junk", std::string(1000, '9')}) {
        const auto p = rs7test::TempIni(
            L"settings", ("[neck]\r\nnative_pitch=" + bad + "\r\n[window]\r\nscreen=" + bad + "\r\n").c_str());

        Rs7Set_InitFromIni(p.c_str());
        CHECK(!Rs7Set_Bool("neck.native_pitch"));
        CHECK_EQ(Rs7Set_Int("window.screen"), 0);
        DeleteFileW(p.c_str());
    }
}
