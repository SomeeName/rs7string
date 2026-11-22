#include "overlay/overlaycmd.h"
#include "platform/settings.h"
#include "test.h"
#include <windows.h>
#include <string>

TEST(overlaycmd_parses_every_verb) {
    RsxrCmd c;

    CHECK(RsxrCmd_Parse("o toggle", &c) && c.kind == RsxrCmdKind::Toggle);
    CHECK(RsxrCmd_Parse("o show\r", &c) && c.kind == RsxrCmdKind::Show);
    CHECK(RsxrCmd_Parse("o  HIDE ", &c) && c.kind == RsxrCmdKind::Hide);
    CHECK(RsxrCmd_Parse("o save", &c) && c.kind == RsxrCmdKind::Save);
    CHECK(RsxrCmd_Parse("o status", &c) && c.kind == RsxrCmdKind::Status);
    CHECK(RsxrCmd_Parse("o tab neck", &c) && c.kind == RsxrCmdKind::Tab && c.key == "neck");
    CHECK(RsxrCmd_Parse("o set neck.string7_v 0.5", &c) && c.kind == RsxrCmdKind::Set && c.key == "neck.string7_v" &&
          c.value == "0.5");
    // The value is the rest of the line (vec3 with blanks, paths with spaces).
    CHECK(RsxrCmd_Parse("o set peg7_offset 0, 0.5, 1 ", &c) && c.key == "peg7_offset" && c.value == "0, 0.5, 1");
    CHECK(RsxrCmd_Parse("o set custom_path D:\\my dumps\\rs", &c) && c.value == "D:\\my dumps\\rs");
}

TEST(overlaycmd_rejects_everything_else) {
    RsxrCmd c;

    CHECK(!RsxrCmd_Parse(nullptr, &c));
    CHECK(!RsxrCmd_Parse("", &c));
    CHECK(!RsxrCmd_Parse("k 1C 1", &c));  // a key line
    CHECK(!RsxrCmd_Parse("otoggle", &c));
    CHECK(!RsxrCmd_Parse("o", &c));
    CHECK(!RsxrCmd_Parse("o explode", &c));
    CHECK(!RsxrCmd_Parse("o dump", &c));
    CHECK(!RsxrCmd_Parse("o toggle now", &c));
    CHECK(!RsxrCmd_Parse("o tab", &c));
    CHECK(!RsxrCmd_Parse("o tab a b", &c));
    CHECK(!RsxrCmd_Parse("o set", &c));
    CHECK(!RsxrCmd_Parse("o set neck.string7_v", &c));
}

TEST(overlaycmd_queue_is_fifo_and_bounded) {
    RsxrCmd c;

    while (RsxrCmd_Pop(&c)) {
    }
    CHECK(RsxrCmd_Post("o show"));
    CHECK(RsxrCmd_Post("o set a 1"));
    CHECK(!RsxrCmd_Post("o nonsense"));
    CHECK(RsxrCmd_Pop(&c) && c.kind == RsxrCmdKind::Show);
    CHECK(RsxrCmd_Pop(&c) && c.kind == RsxrCmdKind::Set && c.key == "a");
    CHECK(!RsxrCmd_Pop(&c));
    int queued = 0;

    for (int i = 0; i < 80; ++i) {
        queued += RsxrCmd_Post("o status") ? 1 : 0;
    }

    CHECK_EQ(queued, 64);
    int popped = 0;

    while (RsxrCmd_Pop(&c)) {
        ++popped;
    }

    CHECK_EQ(popped, 64);
}

TEST(overlaycmd_hotkey_names) {
    CHECK_EQ(Rsxr_ParseHotkey("F9"), VK_F9);
    CHECK_EQ(Rsxr_ParseHotkey(" f12 "), VK_F12);
    CHECK_EQ(Rsxr_ParseHotkey("F24"), VK_F24);
    CHECK_EQ(Rsxr_ParseHotkey("insert"), VK_INSERT);
    CHECK_EQ(Rsxr_ParseHotkey("HOME"), VK_HOME);
    CHECK_EQ(Rsxr_ParseHotkey("0x78"), 0x78);
    CHECK_EQ(Rsxr_ParseHotkey("k"), int('K'));
    CHECK_EQ(Rsxr_ParseHotkey("7"), int('7'));
    CHECK_EQ(Rsxr_ParseHotkey("F0"), 0);
    CHECK_EQ(Rsxr_ParseHotkey("F25"), 0);
    CHECK_EQ(Rsxr_ParseHotkey("0x"), 0);
    CHECK_EQ(Rsxr_ParseHotkey("0xFFF"), 0);
    CHECK_EQ(Rsxr_ParseHotkey(""), 0);
    CHECK_EQ(Rsxr_ParseHotkey("ctrl+f9"), 0);
    CHECK_EQ(Rsxr_ParseHotkey(nullptr), 0);
}

TEST(overlaycmd_removed_archive_settings_are_unknown) {
    CHECK(RsxrSet_Find("debug.dump_assets") < 0);
    CHECK(RsxrSet_Find("debug.dump_dir") < 0);
}
