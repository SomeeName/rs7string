// The per-note report: CSV row, log line, hit rule, per-song totals and the summary line. Values in, values out
// (tests/vguit).
#include "features/vguit/vguit.h"
#include "model/eight.h"
#include <cstdio>
#include <cstring>

namespace rs7vg {

void RecordChord(const Columns& col, const Tuning& t, Record* r) {
    // Lowest used string of the chord for the string/fret columns; game MIDI = its template note.
    for (int s = 0; s < 6; ++s) {
        if (col.fret[s] < 0) {
            continue;
        }

        r->string = s;
        r->fret = col.fret[s];
        r->gameMidi = col.midi[s];
        break;
    }

    if (t.count == 7 && col.fret[6] >= 0) {
        r->s6Fret = col.fret[6];
        r->s6Midi = OurMidi(6, col.fret[6], t.v, t.count);
    }
}

std::string CsvHeader() {
    return "song,idx,time,string,fret,chord,game_midi,our_midi,played_midi,onset,detected,hit_time,offset_ms,"
           "attempts,scored,hit,judged,fwd,back,sustain,mask,s6_fret,s6_midi,s6_played\n";
}

bool IsHit(const Record& r) {
    return r.scored >= 0 ? r.scored == 1 : r.detected != 0;
}

std::string CsvRow(const Record& r) {
    char off[32] = "";

    if (r.detected) {
        std::snprintf(off, sizeof off, "%.1f", (double(r.hitTime) - r.time) * 1000.0);
    }
    char b[512];

    std::snprintf(b, sizeof b,
                  "%d,%d,%.3f,%d,%d,%d,%d,%d,%d,%.4f,%d,%.4f,%s,%d,%d,%d,%d,%.3f,%.3f,%.3f,%08X,%d,%d,%d\n", r.song,
                  r.idx, double(r.time), r.string, r.fret, r.chord ? 1 : 0, r.gameMidi, r.ourMidi, r.played, r.onset,
                  r.detected, double(r.hitTime), off, r.attempts, r.scored, IsHit(r) ? 1 : 0, r.judged ? 1 : 0,
                  double(r.fwd), double(r.back), double(r.sustain), unsigned(r.mask), r.s6Fret, r.s6Midi, r.s6Played);

    return b;
}

std::string LogLine(const Record& r) {
    char b[480];
    char off[32] = "-";
    char s6[64] = "";

    if (r.s6Fret >= 0) {
        std::snprintf(s6, sizeof s6, " +s6 f%d midi %d (played %d)", r.s6Fret, r.s6Midi, r.s6Played);
    }
    if (r.detected) {
        std::snprintf(off, sizeof off, "%+.1f ms", (double(r.hitTime) - r.time) * 1000.0);
    }
    char played[48] = "not played";

    if (r.played >= 0) {
        std::snprintf(played, sizeof played, "played %d @%.3f (%+.1f ms)", r.played, r.onset,
                      (r.onset - r.time) * 1000.0);
    }
    std::snprintf(b, sizeof b,
                  "vguitar: song %d #%d t=%.3f %s s%d f%d midi game %d ours %d%s%s | %s | detected %d hit_t %.3f "
                  "offset %s attempts %d scored %s -> %s (win %+.3f/%+.3f sus %.3f)",
                  r.song, r.idx, double(r.time), r.chord ? "CHORD" : "note", r.string, r.fret, r.gameMidi, r.ourMidi,
                  r.gameMidi != r.ourMidi && r.ourMidi >= 0 ? " (DIFFER)" : "", s6, played, r.detected,
                  double(r.hitTime), off, r.attempts,
                  r.scored < 0 ? "-"
                  : r.scored   ? "1"
                               : "0",
                  IsHit(r) ? "HIT" : "MISS", double(r.back), double(r.fwd), double(r.sustain));

    return b;
}

void Totals::Add(const Record& r) {
    ++notes;
    if (r.chord) {
        ++chords;
    }
    const bool hit = IsHit(r);

    hit ? ++hits : ++misses;

    if (r.chord && r.s6Fret >= 0) {
        ++chords6;
        if (hit) {
            ++chords6Hits;
        }
    }

    if (r.played >= 0) {
        ++played;
        if (hit) {
            ++playedHits;
        }
    } else if (hit) {
        ++unplayedHits;
    }

    const int s = r.string >= 0 && r.string < (g_rs7_eight ? 8 : 7) ? r.string : (g_rs7_eight ? 8 : 7);

    ++perString[s];
    if (hit) {
        ++perStringHits[s];
    }

    if (r.detected && r.played >= 0) {
        const double off = (double(r.hitTime) - r.time) * 1000.0;

        ++offsetCount;
        offsetSum += off;
        if (off < offsetMin) {
            offsetMin = off;
        }
        if (off > offsetMax) {
            offsetMax = off;
        }
    }
}

std::string SummaryLine(int song, const Totals& t, const Counters& c) {
    char b[900];
    int n =
        std::snprintf(b, sizeof b,
                      "vguitar SUMMARY song %d: %d notes (%d chords), %d hits, %d misses, accuracy %.1f %%; played %d, "
                      "played hits %d (%.1f %%), hits on notes not played %d;",
                      song, t.notes, t.chords, t.hits, t.misses, t.notes ? 100.0 * t.hits / t.notes : 0.0, t.played,
                      t.playedHits, t.played ? 100.0 * t.playedHits / t.played : 0.0, t.unplayedHits);

    for (int s = 0; s < (g_rs7_eight ? 9 : 8) && n > 0 && n < int(sizeof b); ++s) {
        if (t.perString[s]) {
            n += std::snprintf(b + n, sizeof b - n, " s%s %d/%d",
                               s < (g_rs7_eight ? 8 : 7) ? std::to_string(s).c_str() : "?", t.perStringHits[s],
                               t.perString[s]);
        }
    }

    if (t.chords6 && n > 0 && n < int(sizeof b)) {
        n += std::snprintf(b + n, sizeof b - n, "; chords with string 6: %d/%d", t.chords6Hits, t.chords6);
    }

    if (n > 0 && n < int(sizeof b)) {
        if (t.offsetCount) {
            n += std::snprintf(b + n, sizeof b - n, "; offset (hit - note) ms mean %+.1f min %+.1f max %+.1f (n=%d)",
                               t.offsetSum / t.offsetCount, t.offsetMin, t.offsetMax, t.offsetCount);
        } else {
            n += std::snprintf(b + n, sizeof b - n, "; no detected played notes");
        }
    }

    if (n > 0 && n < int(sizeof b)) {
        if (c.valid) {
            std::snprintf(b + n, sizeof b - n,
                          "; game: single hit %.0f miss %.0f acc %.3f, total hit %.0f miss %.0f acc %.3f, "
                          "score %.2f (+450h raw %u / %.2f)",
                          double(c.singleHit), double(c.singleMiss), double(c.singleAcc), double(c.totalHit),
                          double(c.totalMiss), double(c.totalAcc), c.score, unsigned(c.scoreShown), [&] {
                              float f;

                              std::memcpy(&f, &c.scoreShown, 4);

                              return double(f);
                          }());
        } else {
            std::snprintf(b + n, sizeof b - n, "; game counters not read");
        }
    }

    return b;
}

}  // namespace rs7vg
