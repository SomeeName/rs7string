// JUD1 / JUD2 (game thread): one record per finished note to rs7string.log and the CSV, the game's own counters, and
// the per-song summary (also from the watchdog thread once the song stops ticking).
#include "features/vguit/hooks.h"
#include "platform/log.h"
#include <cmath>

namespace rs7vg {

void CsvWrite(const std::string& s) {
    if (!g_vg.csv) {
        if (!g_vg.csvWarned) {
            Rs7Log("vguitar: CSV %ls not open", g_vg.csvPath.c_str());
        }
        g_vg.csvWarned = true;
        return;
    }

    std::fputs(s.c_str(), g_vg.csv);
    std::fflush(g_vg.csv);
}

void ReadCounters(void* session, Counters* c) {
    uint8_t b[0x60];
    if (!Rs7_SafeCopy(b, static_cast<uint8_t*>(session) + 0x420, sizeof b)) {
        return;  // 0x420..0x47F
    }

    c->valid = true;
    c->singleAcc = Get<float>(b, 0x428 - 0x420);
    c->singleHit = Get<float>(b, 0x42C - 0x420);
    c->singleMiss = Get<float>(b, 0x430 - 0x420);
    c->totalAcc = Get<float>(b, 0x43C - 0x420);
    c->totalHit = Get<float>(b, 0x440 - 0x420);
    c->totalMiss = Get<float>(b, 0x444 - 0x420);
    c->score = Get<double>(b, 0x448 - 0x420);
    c->scoreShown = Get<uint32_t>(b, 0x450 - 0x420);
}

namespace {
// cs held.
void Emit(const Record& r) {
    Rs7Log("%s", LogLine(r).c_str());
    CsvWrite(CsvRow(r));
    g_vg.totals.Add(r);
    ++g_vg.sinceSummary;
}

// What we played for the note object (the started event of the same note).
void FindPlayed(const void* obj, Record* r) {
    AcquireSRWLockShared(&g_vg.schedLock);

    for (const Event& e : g_vg.ev) {
        if (e.state == kStarted && e.voices > 0 && e.obj == obj && e.time == r->time) {
            r->played = static_cast<int>(std::lround(e.midi[0]));
            r->onset = e.onset;
            r->s6Played = e.s6Played;
            break;
        }
    }

    ReleaseSRWLockShared(&g_vg.schedLock);
}

// The record of a finished note.
bool BuildRecord(const void* obj, Record* r) {
    uint8_t o[kObjSize];
    if (!Rs7_SafeCopy(o, obj, sizeof o)) {
        return false;
    }

    r->time = Get<float>(o, kObjTime);
    r->chord = o[kObjChord] != 0;
    r->gameMidi = Get<int32_t>(o, kObjMidi);
    r->detected = o[kObjDetected];
    r->hitTime = Get<float>(o, kObjHitTime);
    r->attempts = Get<int32_t>(o, kObjAttempts);
    r->fwd = Get<float>(o, kObjFwd);
    r->back = Get<float>(o, kObjBack);
    uint8_t n[0x40];

    if (Rs7_SafeCopy(n, Get<const void*>(o, kObjSng), sizeof n)) {
        r->mask = Get<uint32_t>(n, kSngMask);
        r->string = n[kSngString];
        r->fret = static_cast<int8_t>(n[kSngFret]);
        r->sustain = Get<float>(n, kSngSustain);
    }

    Columns col;

    if (r->chord && ReadColumns(Get<const void*>(o, kObjTemplate), &col)) {
        RecordChord(col, g_vg.tuning, r);
    }

    r->ourMidi = OurMidi(r->string, r->fret, g_vg.tuning.v, g_vg.tuning.count);
    FindPlayed(obj, r);

    return true;
}
}  // namespace

// cs held.
void Summary(const char* why) {
    if (g_vg.hasPending) {
        Emit(g_vg.pending);
        g_vg.hasPending = false;
    }

    if (!g_vg.sinceSummary) {
        return;
    }

    Rs7Log("%s (%s)", SummaryLine(g_vg.song, g_vg.totals, g_vg.counters).c_str(), why);
    if (g_vg.lateNotes) {
        Rs7Log("vguitar: song %d: %d notes first seen too late to play (onset already past)", g_vg.song,
               g_vg.lateNotes);
    }
    char b[64];

    std::snprintf(b, sizeof b, "# summary song %d (%s)\n", g_vg.song, why);
    CsvWrite(b);
    g_vg.sinceSummary = 0;
}

void JudgeImpl(void* note, void* session) {
    Record r;
    if (!BuildRecord(note, &r)) {
        return;
    }

    r.judged = true;
    EnterCriticalSection(&g_vg.cs);

    if (session != g_vg.session) {  // JUD1 runs inside TICK's function: only another player's session
        LeaveCriticalSection(&g_vg.cs);
        return;
    }

    if (g_vg.hasPending) {
        Emit(g_vg.pending);
    }
    r.song = g_vg.song;
    r.idx = ++g_vg.idx;
    g_vg.pending = r;
    g_vg.pendingObj = note;
    g_vg.hasPending = true;
    LeaveCriticalSection(&g_vg.cs);
}

void ScoreImpl(int hit, void* session, void* note) {
    if (session != g_vg.session) {
        return;  // not the session TICK locked onto (game thread: no race)
    }

    float t = 0;

    ReadAt(reinterpret_cast<uintptr_t>(note) + kObjTime, &t);
    EnterCriticalSection(&g_vg.cs);

    if (g_vg.hasPending && g_vg.pendingObj == note && g_vg.pending.time == t) {
        g_vg.pending.scored = hit;
        ReadCounters(session, &g_vg.counters);
        Emit(g_vg.pending);
        g_vg.hasPending = false;
        LeaveCriticalSection(&g_vg.cs);
        return;
    }

    LeaveCriticalSection(&g_vg.cs);
    Record r;  // scored without JUD1 (Play_Session_MissPendingNotes 0x7E2861)
    if (!BuildRecord(note, &r)) {
        return;
    }

    r.scored = hit;
    EnterCriticalSection(&g_vg.cs);

    if (g_vg.hasPending) {
        Emit(g_vg.pending);
        g_vg.hasPending = false;
    }

    r.song = g_vg.song;
    r.idx = ++g_vg.idx;
    ReadCounters(session, &g_vg.counters);
    Emit(r);
    LeaveCriticalSection(&g_vg.cs);
}

DWORD WINAPI Watchdog(void*) {
    for (;;) {
        Sleep(250);
        const DWORD now = GetTickCount();
        if (now - DWORD(g_vg.lastTickMs) < 1500) {
            continue;
        }

        EnterCriticalSection(&g_vg.cs);
        if (g_vg.sinceSummary || g_vg.hasPending) {
            Summary("song stopped ticking");
        }
        LeaveCriticalSection(&g_vg.cs);
    }
}

}  // namespace rs7vg
