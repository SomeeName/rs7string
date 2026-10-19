// What the virtual guitarist plays for a chart note: its pitch, the filter, a chord's voices and a single note's
// techniques as pitch glides. Values in, values out (tests/vguit).
#include "features/vguit/vguit.h"
#include "model/eight.h"

namespace rs7vg {

int OurMidi(int string, int fret, const int16_t* tuning, int n) {
    static const int kOpen[8] = {40, 45, 50, 55, 59, 64, 35, 30};
    if (string < 0 || string > (g_rs7_eight ? 7 : 6) || fret < 0 || fret > 30) {
        return -1;
    }

    const int t = tuning && string < n ? tuning[string] : 0;

    return kOpen[string] + t + fret;
}

bool PassesFilter(const Config& c, int string, int fret) {
    if (string < 0 || string > (g_rs7_eight ? 7 : 6)) {
        return false;
    }
    if (!(c.stringMask & (1u << string))) {
        return false;
    }

    return c.fret < 0 || c.fret == fret;
}

namespace {
// The voice of one chord column; Mode::Midi plays one voice for the whole chord.
int ColumnMidi(const Config& c, const Tuning& t, const Columns& col, int s) {
    if (c.mode == Mode::Midi) {
        return c.midi;
    }

    return c.mode == Mode::Game ? col.midi[s] : OurMidi(s, col.fret[s], t.v, t.count);
}

void AddVoice(Event* e, int m) {
    if (m > 0 && m < 128 && e->voices < 7) {
        e->midi[e->voices++] = float(m);
    }
}

// Negative controls for any chord: leave out its lowest / highest-pitched note.
void OmitExtreme(Chord6 mode, Event* e) {
    int k = 0;

    for (int v = 1; v < e->voices; ++v) {
        if (mode == Chord6::OmitLow ? e->midi[v] < e->midi[k] : e->midi[v] > e->midi[k]) {
            k = v;
        }
    }

    if (e->s6Played >= 0 && e->midi[k] == float(e->s6Played)) {
        e->s6Played = -1;
    }

    for (int v = k; v + 1 < e->voices; ++v) {
        e->midi[v] = e->midi[v + 1];
    }

    --e->voices;
}

void PlanChord(const Config& c, const Tuning& t, const Columns& col, Event* e) {
    const bool has6 = t.count == 7 && col.fret[6] >= 0;

    for (int s = 0; s < 6 && !(has6 && c.chord6 == Chord6::Only); ++s) {
        if (col.fret[s] < 0) {
            continue;
        }

        AddVoice(e, ColumnMidi(c, t, col, s));
        if (c.mode == Mode::Midi) {
            return;
        }
    }

    if (has6 && c.chord6 != Chord6::Omit) {  // the 7th string's column (T8 slot 6)
        const int m = ColumnMidi(c, t, col, 6) + (c.chord6 == Chord6::Shift ? c.chord6Shift : 0);
        const int before = e->voices;

        AddVoice(e, m);
        if (e->voices > before) {
            e->s6Played = m;
        }
    }

    if ((c.chord6 == Chord6::OmitLow || c.chord6 == Chord6::OmitHigh) && e->voices > 1) {
        OmitExtreme(c.chord6, e);
    }
}

// Techniques as pitch glides (seconds after the onset).
Glide NoteGlide(const SngNote& n, float time, float sustain, int fret) {
    Glide g;

    if (n.bendCount > 0 && n.bendCount <= 32) {
        for (uint32_t i = 0; i < n.bendCount; ++i) {
            g.bendT[i] = n.bendTime[i] - time;
            g.bendStep[i] = n.bendStep[i];
        }

        g.bendCount = static_cast<int>(n.bendCount);
    } else if (n.maxBend > 0 && n.maxBend < 5 && sustain > 0) {  // no curve: rise over the first third
        g.bendT[0] = sustain / 3;
        g.bendStep[0] = n.maxBend;
        g.bendCount = 1;
    }

    const int target = n.slideTo >= 0 ? n.slideTo : n.slideUnpitchedTo;

    if (target >= 0 && sustain > 0 && fret >= 0) {
        g.slideSemis = float(target - fret);
        g.slideStart = 0;
        g.slideEnd = sustain;
    }

    if (n.vibrato) {
        g.vibratoSemis = 0.3f;
    }

    return g;
}

int NoteMidi(const Config& c, const Tuning& t, const Event& e, int gameMidi) {
    switch (c.mode) {
        case Mode::Chart:
            return OurMidi(e.string, e.fret, t.v, t.count);
        case Mode::Game:
            return gameMidi;
        case Mode::Midi:
            return c.midi;
        case Mode::Off:
            break;
    }

    return -1;
}
}  // namespace

void PlanEvent(const Config& c, const Tuning& t, const SngNote& note, const Columns* chord, int gameMidi, Event* e) {
    e->string = note.string;
    e->fret = note.fret;
    e->sustain = note.sustain >= 0 && note.sustain < 60 ? note.sustain : 0;
    e->voices = 0;
    e->s6Played = -1;
    if (c.mode == Mode::Off) {
        return;
    }

    if (e->chord) {
        if (c.filterAll && chord) {
            PlanChord(c, t, *chord, e);
        }
        return;
    }

    if (!PassesFilter(c, e->string, e->fret)) {
        return;
    }

    const int m = NoteMidi(c, t, *e, gameMidi);
    if (m <= 0 || m >= 128) {
        return;
    }

    e->midi[0] = float(m);
    e->voices = 1;
    e->glide = NoteGlide(note, e->time, e->sustain, e->fret);
}

}  // namespace rs7vg
