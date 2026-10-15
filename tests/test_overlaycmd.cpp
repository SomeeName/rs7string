#include "core/overlaycmd.h"
#include "test.h"
#include <windows.h>
#include <string>

TEST(overlaycmd_parses_every_verb) {
    Rs7Cmd c;

    CHECK(Rs7Cmd_Parse("o toggle", &c) && c.kind == Rs7CmdKind::Toggle);
    CHECK(Rs7Cmd_Parse("o show\r", &c) && c.kind == Rs7CmdKind::Show);
    CHECK(Rs7Cmd_Parse("o  HIDE ", &c) && c.kind == Rs7CmdKind::Hide);
    CHECK(Rs7Cmd_Parse("o save", &c) && c.kind == Rs7CmdKind::Save);
    CHECK(Rs7Cmd_Parse("o dump", &c) && c.kind == Rs7CmdKind::Dump);
    CHECK(Rs7Cmd_Parse("o status", &c) && c.kind == Rs7CmdKind::Status);
    CHECK(Rs7Cmd_Parse("o tab neck", &c) && c.kind == Rs7CmdKind::Tab && c.key == "neck");
    CHECK(Rs7Cmd_Parse("o set neck.string7_v 0.5", &c) && c.kind == Rs7CmdKind::Set && c.key == "neck.string7_v" &&
          c.value == "0.5");
    // The value is the rest of the line (vec3 with blanks, paths with spaces).
    CHECK(Rs7Cmd_Parse("o set peg7_offset 0, 0.5, 1 ", &c) && c.key == "peg7_offset" && c.value == "0, 0.5, 1");
    CHECK(Rs7Cmd_Parse("o set dump_dir D:\\my dumps\\rs", &c) && c.value == "D:\\my dumps\\rs");
}

TEST(overlaycmd_rejects_everything_else) {
    Rs7Cmd c;

    CHECK(!Rs7Cmd_Parse(nullptr, &c));
    CHECK(!Rs7Cmd_Parse("", &c));
    CHECK(!Rs7Cmd_Parse("k 1C 1", &c));  // a key line
    CHECK(!Rs7Cmd_Parse("otoggle", &c));
    CHECK(!Rs7Cmd_Parse("o", &c));
    CHECK(!Rs7Cmd_Parse("o explode", &c));
    CHECK(!Rs7Cmd_Parse("o toggle now", &c));
    CHECK(!Rs7Cmd_Parse("o tab", &c));
    CHECK(!Rs7Cmd_Parse("o tab a b", &c));
    CHECK(!Rs7Cmd_Parse("o set", &c));
    CHECK(!Rs7Cmd_Parse("o set neck.string7_v", &c));
}

TEST(overlaycmd_queue_is_fifo_and_bounded) {
    Rs7Cmd c;

    while (Rs7Cmd_Pop(&c)) {
    }
    CHECK(Rs7Cmd_Post("o show"));
    CHECK(Rs7Cmd_Post("o set a 1"));
    CHECK(!Rs7Cmd_Post("o nonsense"));
    CHECK(Rs7Cmd_Pop(&c) && c.kind == Rs7CmdKind::Show);
    CHECK(Rs7Cmd_Pop(&c) && c.kind == Rs7CmdKind::Set && c.key == "a");
    CHECK(!Rs7Cmd_Pop(&c));
    int queued = 0;

    for (int i = 0; i < 80; ++i) {
        queued += Rs7Cmd_Post("o status") ? 1 : 0;
    }

    CHECK_EQ(queued, 64);
    int popped = 0;

    while (Rs7Cmd_Pop(&c)) {
        ++popped;
    }

    CHECK_EQ(popped, 64);
}

TEST(overlaycmd_hotkey_names) {
    CHECK_EQ(Rs7_ParseHotkey("F9"), VK_F9);
    CHECK_EQ(Rs7_ParseHotkey(" f12 "), VK_F12);
    CHECK_EQ(Rs7_ParseHotkey("F24"), VK_F24);
    CHECK_EQ(Rs7_ParseHotkey("insert"), VK_INSERT);
    CHECK_EQ(Rs7_ParseHotkey("HOME"), VK_HOME);
    CHECK_EQ(Rs7_ParseHotkey("0x78"), 0x78);
    CHECK_EQ(Rs7_ParseHotkey("k"), int('K'));
    CHECK_EQ(Rs7_ParseHotkey("7"), int('7'));
    CHECK_EQ(Rs7_ParseHotkey("F0"), 0);
    CHECK_EQ(Rs7_ParseHotkey("F25"), 0);
    CHECK_EQ(Rs7_ParseHotkey("0x"), 0);
    CHECK_EQ(Rs7_ParseHotkey("0xFFF"), 0);
    CHECK_EQ(Rs7_ParseHotkey(""), 0);
    CHECK_EQ(Rs7_ParseHotkey("ctrl+f9"), 0);
    CHECK_EQ(Rs7_ParseHotkey(nullptr), 0);
}
