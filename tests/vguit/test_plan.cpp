#include "features/vguit/vguit.h"
#include "model/eight.h"
#include "test.h"
#include <cstring>

using namespace rs7vg;

namespace {
Tuning Seven(int16_t t6 = 0) {
    Tuning t;

    t.count = 7;
    t.v[6] = t6;

    return t;
}

// A chord: strings 0 and 2 (frets 3, 2), optionally string 6 at `s6`; the template MIDI is 100 + string.
Columns Chord(int s6 = -1) {
    Columns c;

    for (int s = 0; s < 8; ++s) {
        c.fret[s] = -1;
        c.midi[s] = 100 + s;
    }

    c.fret[0] = 3;
    c.fret[2] = 2;
    c.fret[6] = s6;

    return c;
}

Event Plan(const Config& c, const Tuning& t, const SngNote& n, const Columns* chord, int gameMidi = 77) {
    Event e;

    e.chord = chord != nullptr;
    e.time = 10;
    PlanEvent(c, t, n, chord, gameMidi, &e);

    return e;
}
}  // namespace

TEST(vguit_plan_single_note_pitch_per_mode) {
    Config c;
    SngNote n;

    n.string = 6;
    n.fret = 2;
    CHECK(Plan(c, Seven(-1), n, nullptr).voices == 1 && Plan(c, Seven(-1), n, nullptr).midi[0] == 36.0f);
    c.mode = Mode::Game;
    CHECK(Plan(c, Seven(), n, nullptr).midi[0] == 77.0f);
    c.mode = Mode::Midi;
    c.midi = 36;
    CHECK(Plan(c, Seven(), n, nullptr).midi[0] == 36.0f);
    c.mode = Mode::Off;
    CHECK_EQ(Plan(c, Seven(), n, nullptr).voices, 0);
    c.mode = Mode::Chart;
    c.stringMask = 0x01;  // filter: string 0 only
    CHECK_EQ(Plan(c, Seven(), n, nullptr).voices, 0);
}

TEST(vguit_plan_sustain_and_glides) {
    Config c;
    SngNote n;

    n.string = 0;
    n.fret = 5;
    n.sustain = 99;  // out of range -> 0
    CHECK(Plan(c, Seven(), n, nullptr).sustain == 0);
    n.sustain = 1.5f;
    n.bendCount = 2;
    n.bendTime[0] = 10.5f;
    n.bendStep[0] = 1;
    n.bendTime[1] = 11;
    n.bendStep[1] = 2;
    n.slideTo = 7;
    n.vibrato = true;
    const Event e = Plan(c, Seven(), n, nullptr);

    CHECK(e.glide.bendCount == 2 && e.glide.bendT[0] == 0.5f && e.glide.bendStep[1] == 2);
    CHECK(e.glide.slideSemis == 2 && e.glide.slideEnd == 1.5f && e.glide.vibratoSemis == 0.3f);
    n.bendCount = 0;
    n.maxBend = 1;  // no curve: rise over the first third
    const Event m = Plan(c, Seven(), n, nullptr);

    CHECK(m.glide.bendCount == 1 && m.glide.bendT[0] == 0.5f && m.glide.bendStep[0] == 1);
}

TEST(vguit_plan_chord_voices_and_string6_controls) {
    Config c;
    SngNote n;
    const Columns plain = Chord(), with6 = Chord(4);

    CHECK_EQ(Plan(c, Seven(), n, &plain).voices, 2);  // 43, 52
    const Event p = Plan(c, Seven(), n, &with6);

    CHECK(p.voices == 3 && p.midi[2] == 39.0f && p.s6Played == 39);
    c.chord6 = Chord6::Omit;
    CHECK(Plan(c, Seven(), n, &with6).voices == 2 && Plan(c, Seven(), n, &with6).s6Played == -1);
    c.chord6 = Chord6::Only;
    CHECK(Plan(c, Seven(), n, &with6).voices == 1 && Plan(c, Seven(), n, &with6).midi[0] == 39.0f);
    c.chord6 = Chord6::Shift;
    c.chord6Shift = -2;
    CHECK(Plan(c, Seven(), n, &with6).s6Played == 37);
    c.chord6 = Chord6::OmitLow;  // drops the string-6 note, the lowest
    const Event low = Plan(c, Seven(), n, &with6);

    CHECK(low.voices == 2 && low.s6Played == -1 && low.midi[0] == 43.0f && low.midi[1] == 52.0f);
    c.chord6 = Chord6::OmitHigh;
    CHECK(Plan(c, Seven(), n, &with6).voices == 2 && Plan(c, Seven(), n, &with6).s6Played == 39);
    c.chord6 = Chord6::Play;
    c.mode = Mode::Midi;
    c.midi = 30;
    CHECK(Plan(c, Seven(), n, &with6).voices == 1 && Plan(c, Seven(), n, &with6).midi[0] == 30.0f);
    c.mode = Mode::Game;
    CHECK(Plan(c, Seven(), n, &with6).midi[2] == 106.0f);
    c.filterAll = false;  // a string filter plays no chords
    CHECK_EQ(Plan(c, Seven(), n, &with6).voices, 0);
}

TEST(vguit_record_chord_columns) {
    Record r;
    const Columns col = Chord(4);

    RecordChord(col, Seven(-1), &r);
    CHECK(r.string == 0 && r.fret == 3 && r.gameMidi == 100);
    CHECK(r.s6Fret == 4 && r.s6Midi == 38);
    Record six;
    Tuning t;  // a 6-string tuning: no string-6 column

    RecordChord(col, t, &six);
    CHECK_EQ(six.s6Fret, -1);
}

namespace {
Player g_player;  // 10 voices of 32 KB: not on the stack
float g_mix[4800];
}  // namespace

TEST(vguit_player_starts_each_onset_at_its_offset) {
    Event ev[2];

    ev[0].voices = 1;
    ev[0].midi[0] = 45;
    ev[0].time = 1.0f;
    ev[1].voices = 2;
    ev[1].midi[0] = 52;
    ev[1].midi[1] = 57;
    ev[1].time = 1.02f;
    Onset on[2] = {{2000, 1}, {1000, 0}};  // unsorted

    g_player.Render(g_mix, 4800, on, 2, ev, 0.3f);
    CHECK(Rms(g_mix, 1000) == 0);
    CHECK(Rms(g_mix + 1000, 1000) > 0);
    CHECK_EQ(g_player.Started(), 3u);
    g_player.ReleaseAll();
    g_player.Render(g_mix, 4800, nullptr, 0, ev, 0.3f);
    CHECK(Rms(g_mix + 2400, 2400) == 0);  // released voices end after the 30 ms ramp
}
