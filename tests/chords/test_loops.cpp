#include "config_fixture.h"
#include "features/chords/chords.h"
#include "features/strings/strings.h"
#include "hook_rules.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

TEST(chordloops_default_on_and_requires_native_owner) {
    const Rs7TestConfig c = Rs7Test_LoadConfig(L"Z:\\missing\\chordloops.ini");

    CHECK(c.nativeChordloops);
    const Rs7Hook* h = Rs7_FindHook("CLGU");
    const Rs7Hook* legacy = Rs7_FindHook("N22");

    CHECK(h && legacy);
    if (!h || !legacy) {
        return;
    }

    const auto old = g_rs7_native_chordloops;

    g_rs7_native_chordloops = 0;
    CHECK(!Rs7_ShouldPatch(*h, false, false, false, true));
    CHECK(Rs7_ShouldPatch(*legacy, false, false, false, true));
    g_rs7_native_chordloops = 1;
    CHECK(!Rs7_ShouldPatch(*h, false, false, false, false));
    CHECK(Rs7_ShouldPatch(*legacy, false, false, false, false));
    CHECK(Rs7_ShouldPatch(*h, false, false, false, true));
    CHECK(!Rs7_ShouldPatch(*legacy, false, false, false, true));

    for (const char* id : {"NF2", "NF3", "CT4", "CT5", "CT6", "CT7", "CT8", "CT9", "CT10", "CT11"}) {
        const Rs7Hook* oldHook = Rs7_FindHook(id);

        CHECK(oldHook && !Rs7_ShouldPatch(*oldHook, false, false, false, true));
    }

    g_rs7_native_chordloops = old;
}

TEST(chordloops_ini_is_a_startup_opt_in_in_the_chords_section) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];

    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"ccl", 0, path);
    FILE* f = nullptr;

    _wfopen_s(&f, path, L"wb");
    CHECK(f != nullptr);
    if (!f) {
        return;
    }

    std::fputs("[neck]\r\nnative_strings=0\r\n[chords]\r\nnative_chordloops=1\r\n", f);
    std::fclose(f);
    const Rs7TestConfig c = Rs7Test_LoadConfig(path);

    CHECK(c.nativeChordloops);
    CHECK_EQ(c.nativeChordloopsRaw, 1);
    CHECK_EQ(c.removedKeys, 1);  // native_strings (removed in 1.0) is ignored; activation belongs to startup/registry
    DeleteFileW(path);
}

TEST(chordloops_frame_top_is_off_without_the_switch_or_for_six_strings) {
    const auto old = g_rs7_native_chordloops;
    uint8_t tpl[0x80];

    std::memset(tpl, 0xFF, sizeof tpl);
    g_rs7_native_chordloops = 0;
    Rs7Chord_SetSongStrings(8);
    CHECK_EQ(Rs7Chord_FrameTop(tpl), -1);
    g_rs7_native_chordloops = 1;
    Rs7Chord_SetSongStrings(6);
    CHECK_EQ(Rs7Chord_FrameTop(tpl), -1);
    Rs7Chord_SetSongStrings(8);
    CHECK_EQ(Rs7Chord_FrameTop(tpl), -1);  // not a registered T8 record
    Rs7Chord_SetSongStrings(6);
    g_rs7_native_chordloops = old;
}

#include "features/nslot/nslot.h"

TEST(chordloops_frame_top_finds_the_highest_added_string_of_a_t8) {
    const auto old = g_rs7_native_chordloops;
    alignas(4) static uint8_t t8[2 * rs7ns::kT8Size];

    std::memset(t8, 0xFF, sizeof t8);
    t8[rs7ns::kT8Frets + 6] = 3;                   // record 0: string 6 only
    t8[rs7ns::kT8Size + rs7ns::kT8Frets + 7] = 5;  // record 1: string 7 only
    Rs7NsEntry& e = g_rs7_ns[kRs7NsEntries - 1];
    const Rs7NsEntry saved = e;

    e = {};
    e.tplBegin = 0x1000;
    e.tplCount = 2;
    e.t8 = reinterpret_cast<uintptr_t>(t8);
    g_rs7_native_chordloops = 1;
    Rs7Chord_SetSongStrings(8);
    CHECK_EQ(Rs7Chord_FrameTop(t8), 6);
    CHECK_EQ(Rs7Chord_FrameTop(t8 + rs7ns::kT8Size), 7);
    Rs7Chord_SetSongStrings(7);
    CHECK_EQ(Rs7Chord_FrameTop(t8 + rs7ns::kT8Size), -1);  // slot 7 is not a string of a 7-string song
    CHECK_EQ(Rs7Chord_FrameTop(t8), 6);
    Rs7Chord_SetSongStrings(6);
    g_rs7_native_chordloops = old;
    e = saved;
}
