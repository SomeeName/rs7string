#include "features/vguit/vguit.h"
#include "model/eight.h"
#include "tempfile.h"
#include "test.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <string>

using namespace rs7vg;

TEST(vguit_mode_parse) {
    Mode m = Mode::Off;
    int midi = -1;

    CHECK(ParseMode(L"chart", &m, &midi) && m == Mode::Chart);
    CHECK(ParseMode(L" Game ", &m, &midi) && m == Mode::Game);
    CHECK(ParseMode(L"off", &m, &midi) && m == Mode::Off);
    CHECK(ParseMode(L"midi:36", &m, &midi) && m == Mode::Midi && midi == 36);
    CHECK(!ParseMode(L"midi:", &m, &midi));
    CHECK(!ParseMode(L"midi:3x", &m, &midi));
    CHECK(!ParseMode(L"midi:200", &m, &midi));
    CHECK(!ParseMode(L"loud", &m, &midi));
}

TEST(vguit_filter_parse) {
    uint8_t mask = 0;
    int fret = 0;
    bool all = false;

    CHECK(ParseFilter(L"all", &mask, &fret, &all) && all && mask == 0x7F && fret == -1);
    CHECK(ParseFilter(L"6", &mask, &fret, &all) && !all && mask == 0x40 && fret == -1);
    CHECK(ParseFilter(L"056", &mask, &fret, &all) && mask == 0x61);
    CHECK(ParseFilter(L"6:0", &mask, &fret, &all) && mask == 0x40 && fret == 0);
    CHECK(!ParseFilter(L"7", &mask, &fret, &all));
    CHECK(!ParseFilter(L":3", &mask, &fret, &all));
    CHECK(!ParseFilter(L"6:", &mask, &fret, &all));
    CHECK(!ParseFilter(L"6:x", &mask, &fret, &all));
}

TEST(vguit_float_parse) {
    float v = 0;

    CHECK(ParseFloat(L"0.3", 0, 1, &v) && v == 0.3f);
    CHECK(ParseFloat(L" -25 ", -500, 500, &v) && v == -25.0f);
    CHECK(!ParseFloat(L"2", 0, 1, &v));
    CHECK(!ParseFloat(L"abc", 0, 1, &v));
    CHECK(!ParseFloat(L"", 0, 1, &v));
}

TEST(vguit_our_midi) {
    const int16_t estd7[7] = {0, 0, 0, 0, 0, 0, 0};

    CHECK_EQ(OurMidi(6, 0, estd7, 7), 35);  // the RS7 Test sheet
    CHECK_EQ(OurMidi(6, 3, estd7, 7), 38);
    CHECK_EQ(OurMidi(6, 5, estd7, 7), 40);
    CHECK_EQ(OurMidi(6, 7, estd7, 7), 42);
    CHECK_EQ(OurMidi(0, 7, estd7, 7), 47);
    CHECK_EQ(OurMidi(5, 3, estd7, 7), 67);
    const int16_t dropA[7] = {-2, 0, 0, 0, 0, 0, -2};

    CHECK_EQ(OurMidi(0, 0, dropA, 7), 38);
    CHECK_EQ(OurMidi(6, 0, dropA, 7), 33);
    CHECK_EQ(OurMidi(6, 2, dropA, 6), 37);  // 6-entry vector: t6 = 0
    CHECK_EQ(OurMidi(6, 0, nullptr, 0), 35);
    CHECK_EQ(OurMidi(7, 0, estd7, 7), -1);
    CHECK_EQ(OurMidi(0, -1, estd7, 7), -1);
}

TEST(vguit_filter_applies_to_single_notes) {
    Config c;

    CHECK(PassesFilter(c, 0, 3) && PassesFilter(c, 6, 7));
    c.stringMask = 0x40;
    c.fret = 0;
    c.filterAll = false;
    CHECK(PassesFilter(c, 6, 0));
    CHECK(!PassesFilter(c, 6, 3));
    CHECK(!PassesFilter(c, 0, 0));
    CHECK(!PassesFilter(c, 7, 0));
}

TEST(vguit_config_defaults_and_ini) {
    const Config d = LoadConfig(rs7test::TempIni(L"vguit", "[other]\nx=1\n").c_str());

    CHECK(!d.on && d.mode == Mode::Chart && d.filterAll && d.leadMs == 0 && d.gain == 0.3f && !d.mix);
    const Config c =
        LoadConfig(rs7test::TempIni(L"vguit", "[test]\nvguitar=1\nvguitar_mode=midi:36\nvguitar_filter=6:0\n"
                                              "vguitar_lead_ms=-20\nvguitar_gain=0.2\nvguitar_mix=1\n")
                       .c_str());

    CHECK(c.on && c.mode == Mode::Midi && c.midi == 36 && !c.filterAll && c.stringMask == 0x40 && c.fret == 0);
    CHECK(c.leadMs == -20.0f && c.gain == 0.2f && c.mix);
    const Config bad = LoadConfig(
        rs7test::TempIni(L"vguit", "[test]\nvguitar=1\nvguitar_mode=x\nvguitar_gain=5\nvguitar_filter=9\n").c_str());

    CHECK(!bad.modeValid && bad.mode == Mode::Chart);
    CHECK(!bad.gainValid && bad.gain == 0.3f);
    CHECK(!bad.filterValid && bad.filterAll);
}

TEST(vguit_csv_row_has_every_column) {
    Record r;

    r.song = 1;
    r.idx = 9;
    r.time = 15.0f;
    r.string = 6;
    r.fret = 0;
    r.gameMidi = 35;
    r.ourMidi = 35;
    r.played = 35;
    r.onset = 14.99;
    r.detected = 1;
    r.hitTime = 15.042f;
    r.attempts = 3;
    r.scored = 1;
    r.judged = true;
    const std::string row = CsvRow(r), head = CsvHeader();

    CHECK_EQ(std::count(row.begin(), row.end(), ','), std::count(head.begin(), head.end(), ','));
    CHECK(row.find(",42.0,") != std::string::npos);  // offset_ms
    CHECK(row.back() == '\n');
    r.detected = 0;
    const std::string miss = CsvRow(r);

    CHECK_EQ(std::count(miss.begin(), miss.end(), ','), std::count(head.begin(), head.end(), ','));
}

TEST(vguit_hit_rule_and_totals) {
    Record a;  // scored by JUD2

    a.scored = 1;
    CHECK(IsHit(a));
    a.scored = 0;
    a.detected = 1;
    CHECK(!IsHit(a));  // JUD2 decides when it ran
    Record b;

    b.detected = 1;  // not scored: JUD1's detected flag
    CHECK(IsHit(b));
    Totals t;
    Record p;

    p.string = 6;
    p.played = 35;
    p.detected = 1;
    p.scored = 1;
    p.time = 15;
    p.hitTime = 15.05f;
    t.Add(p);
    Record q;

    q.string = 0;
    q.scored = 0;
    t.Add(q);
    Record u;

    u.string = 0;
    u.scored = 1;  // hit although nothing was played
    t.Add(u);
    CHECK_EQ(t.notes, 3);
    CHECK_EQ(t.hits, 2);
    CHECK_EQ(t.played, 1);
    CHECK_EQ(t.playedHits, 1);
    CHECK_EQ(t.unplayedHits, 1);
    CHECK_EQ(t.perString[6], 1);
    CHECK_EQ(t.perStringHits[0], 1);
    CHECK_EQ(t.offsetCount, 1);
    Counters c;
    const std::string s = SummaryLine(1, t, c);

    CHECK(s.find("3 notes") != std::string::npos && s.find("2 hits") != std::string::npos);
    CHECK(s.find("s6 1/1") != std::string::npos && s.find("not played 1") != std::string::npos);
}

TEST(vguit_eight_pitch_filter_and_legacy_gate) {
    const auto saved = g_rs7_eight;
    const int16_t t[8] = {0, 0, 0, 0, 0, 0, 0, -2};
    Config c;
    uint8_t mask = 0;
    int fret = -1;
    bool all = false;

    g_rs7_eight = 0;
    CHECK_EQ(OurMidi(7, 0, t, 8), -1);
    CHECK(!ParseFilter(L"7", &mask, &fret, &all));
    g_rs7_eight = 1;
    CHECK_EQ(OurMidi(7, 0, t, 8), 28);
    CHECK_EQ(OurMidi(7, 12, t, 8), 40);
    CHECK_EQ(OurMidi(8, 0, t, 8), -1);
    CHECK(ParseFilter(L"all", &mask, &fret, &all));
    c.stringMask = mask;
    CHECK(PassesFilter(c, 7, 0));
    CHECK(ParseFilter(L"67:3", &mask, &fret, &all));
    CHECK_EQ(mask, 0xC0);
    CHECK_EQ(fret, 3);
    const Config default8 = LoadConfig(rs7test::TempIni(L"vguit", "[other]\nx=1\n").c_str());

    CHECK_EQ(default8.stringMask, 0xFF);
    const Config filter8 = LoadConfig(rs7test::TempIni(L"vguit", "[test]\nvguitar_filter=7:0\n").c_str());

    CHECK(filter8.filterValid && filter8.stringMask == 0x80 && filter8.fret == 0);
    g_rs7_eight = saved;
}

TEST(vguit_chord6_parse) {
    Chord6 m = Chord6::Play;
    int sh = 0;

    CHECK(ParseChord6(L"omit", &m, &sh) && m == Chord6::Omit);
    CHECK(ParseChord6(L" Only ", &m, &sh) && m == Chord6::Only);
    CHECK(ParseChord6(L"play", &m, &sh) && m == Chord6::Play);
    CHECK(ParseChord6(L"+1", &m, &sh) && m == Chord6::Shift && sh == 1);
    CHECK(ParseChord6(L"-12", &m, &sh) && m == Chord6::Shift && sh == -12);
    CHECK(!ParseChord6(L"+0", &m, &sh));
    CHECK(!ParseChord6(L"+30", &m, &sh));
    CHECK(!ParseChord6(L"3", &m, &sh));
    CHECK(!ParseChord6(L"x", &m, &sh));
}
