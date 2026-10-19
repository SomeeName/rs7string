// TICK (game thread): follows the play session (new song, restart, time jumps) and turns the live note objects into
// the schedule the audio thread plays.
#include "features/nslot/nslot.h"
#include "features/vguit/hooks.h"
#include "platform/log.h"

namespace rs7vg {

static_assert(sizeof(Columns::fret) / sizeof(int) == rs7ns::kSlots, "a chord column per N-slot");

bool MasterClock(float* clock) {
    uintptr_t fg = 0;

    return g_rs7_frame_global && ReadAt(g_rs7_frame_global, &fg) && fg && ReadAt(fg + 0xD8, clock);
}

// A chord's columns (strings 0-7) from its template: the T8 record's N-slot arrays (features/nslot/nslot.h; every
// template pointer at play time is one), else the six stock columns.
bool ReadColumns(const void* tpl, Columns* c) {
    for (int s = 0; s < rs7ns::kSlots; ++s) {
        c->fret[s] = c->midi[s] = -1;
    }

    uint8_t t[rs7ns::kT8Size];
    const bool t8 = Rs7Ns_AsT8(tpl) != nullptr;
    if (!Rs7_SafeCopy(t, tpl, t8 ? rs7ns::kT8Size : kTplSize)) {
        return false;
    }

    const uint32_t frets = t8 ? rs7ns::kT8Frets : kTplFrets, notes = t8 ? rs7ns::kT8Midi : kTplNotes;

    for (int s = 0; s < (t8 ? rs7ns::kSlots : 6); ++s) {
        c->fret[s] = static_cast<int8_t>(t[frets + s]);
        std::memcpy(&c->midi[s], t + notes + 4 * s, 4);
    }

    return true;
}

namespace {
SngNote ReadSngNote(const uint8_t* n) {
    SngNote s;

    s.string = n[kSngString];
    s.fret = static_cast<int8_t>(n[kSngFret]);
    s.sustain = Get<float>(n, kSngSustain);
    s.bendCount = Get<uint32_t>(n, kSngBendCount);

    for (uint32_t i = 0; i < s.bendCount && i < 32; ++i) {
        s.bendTime[i] = Get<float>(n, kSngBend + i * 0xC);
        s.bendStep[i] = Get<float>(n, kSngBend + i * 0xC + 4);
    }

    s.maxBend = Get<float>(n, kSngMaxBend);
    s.slideTo = static_cast<int8_t>(n[kSngSlideTo]);
    s.slideUnpitchedTo = static_cast<int8_t>(n[kSngSlideUnp]);
    s.vibrato = n[kSngVibrato] != 0;

    return s;
}

// A new event from a live note object (schedule lock NOT held).
bool BuildEvent(const void* obj, const uint8_t* o, Event* e) {
    e->obj = obj;
    e->time = Get<float>(o, kObjTime);
    e->chord = o[kObjChord] != 0;
    uint8_t n[kSngSize];
    if (!Rs7_SafeCopy(n, Get<const void*>(o, kObjSng), sizeof n)) {
        return false;
    }

    Columns col;
    const bool chord = e->chord && ReadColumns(Get<const void*>(o, kObjTemplate), &col);

    PlanEvent(g_vg.cfg, g_vg.tuning, ReadSngNote(n), chord ? &col : nullptr, Get<int32_t>(o, kObjMidi), e);

    return true;
}

// Frees the events in state `which` (kFree: all of them) and asks the audio thread to drop its voices.
void ClearSchedule(uint8_t which) {
    AcquireSRWLockExclusive(&g_vg.schedLock);

    for (Event& e : g_vg.ev) {
        if (which == kFree || e.state == which) {
            e.state = kFree;
        }
    }

    ReleaseSRWLockExclusive(&g_vg.schedLock);
    InterlockedExchange(&g_vg.flush, 1);
}

// cs held.
void NewSession(void* session) {
    Summary("next session");

    {  // every [test] key but vguitar itself is re-read at each song start
        Config c = LoadConfig(g_vg.iniPath.c_str());

        c.on = g_vg.cfg.on;
        g_vg.cfg = c;
    }

    const Config& cfg = g_vg.cfg;
    const int16_t* tn = g_vg.tuning.v;

    g_vg.session = session;
    ++g_vg.song;
    g_vg.idx = 0;
    g_vg.totals = Totals{};
    g_vg.counters = Counters{};
    g_vg.aheadLogged = false;
    g_vg.lateNotes = 0;
    int32_t input = -1;

    ReadAt(reinterpret_cast<uintptr_t>(session) + kSessInput, &input);
    InterlockedExchange(&g_vg.sessionInput, input);
    ClearSchedule(kFree);
    const bool tuned = g_vg.tuneSession == session;

    Rs7Log("vguitar: song %d session %p input %d, tuning %s (%d entries: %d %d %d %d %d %d %d - unused entries 0), "
           "mode %s%s, filter %ls, "
           "lead %.1f ms, gain %.2f, %s, chord6 %s %d",
           g_vg.song, session, input, tuned ? "from S4" : "NOT SEEN (E standard assumed)", g_vg.tuning.count, tn[0],
           tn[1], tn[2], tn[3], tn[4], tn[5], tn[6], ModeName(cfg.mode),
           cfg.mode == Mode::Midi ? (" " + std::to_string(cfg.midi)).c_str() : "",
           cfg.filterAll ? L"all" : cfg.filterRaw.c_str(), double(cfg.leadMs), double(cfg.gain),
           cfg.mix ? "mix" : "replace", Chord6Name(cfg.chord6), cfg.chord6Shift);
    if (!tuned) {
        g_vg.tuning.count = 0;
    }
    float clock = 0;

    MasterClock(&clock);
    Rs7Log("vguitar: song %d start: master clock %.3f", g_vg.song, double(clock));
}

// Session switch, restart and time jumps; false: another player's session (ignored).
bool TrackSession(void* session, float time) {
    EnterCriticalSection(&g_vg.cs);

    if (session != g_vg.session) {
        // Another session while ours still ticks (multiplayer): keep the first one.
        if (g_vg.session && GetTickCount() - g_vg.ownTickMs < 1000) {
            LeaveCriticalSection(&g_vg.cs);
            return false;
        }

        NewSession(session);
    }

    g_vg.ownTickMs = GetTickCount();
    InterlockedExchange(&g_vg.lastTickMs, LONG(g_vg.ownTickMs));
    InterlockedExchange(&g_vg.active, 1);

    if (time < 0.5f && g_vg.lastTime > 5.0f) {
        // The same session restarted from the top (results screen -> play again): a new run with its
        // own summary and freshly read [test] keys.
        Rs7Log("vguitar: song restarted (%.3f -> %.3f): new run", double(g_vg.lastTime), double(time));
        NewSession(session);
    } else if (time < g_vg.lastTime - 0.5f) {
        Rs7Log("vguitar: song time went back %.3f -> %.3f: voices and schedule reset", double(g_vg.lastTime),
               double(time));
        ClearSchedule(kPending);
    }

    g_vg.lastTime = time;
    ReadCounters(session, &g_vg.counters);
    LeaveCriticalSection(&g_vg.cs);

    return true;
}

void PruneSchedule(float time) {
    AcquireSRWLockExclusive(&g_vg.schedLock);

    for (Event& e : g_vg.ev) {
        if (e.state == kFree) {
            continue;
        }

        const float end = e.time + e.sustain + 2.0f;

        if (end < time) {
            e.state = kFree;
        }
    }

    ReleaseSRWLockExclusive(&g_vg.schedLock);
}

bool IsScheduled(const void* obj, float t) {
    bool known = false;

    AcquireSRWLockShared(&g_vg.schedLock);

    for (const Event& e : g_vg.ev) {
        if (e.state != kFree && e.obj == obj && e.time == t) {
            known = true;
            break;
        }
    }

    ReleaseSRWLockShared(&g_vg.schedLock);

    return known;
}

void Schedule(const Event& ev) {
    AcquireSRWLockExclusive(&g_vg.schedLock);
    Event* slot = nullptr;

    for (Event& e : g_vg.ev) {
        if (e.state == kFree) {
            slot = &e;
            break;
        }
    }

    if (slot) {
        *slot = ev;
    }
    ReleaseSRWLockExclusive(&g_vg.schedLock);

    if (!slot) {
        if (!g_vg.scheduleFullLogged) {
            Rs7Log("vguitar: schedule full (%d events)", kEvents);
        }
        g_vg.scheduleFullLogged = true;
    }
}

// One live note object: scheduled once, marked skipped when its onset is already past.
void ScheduleNote(const void* obj, float time, float* maxAhead) {
    uint8_t o[kObjSize];
    if (!obj || !Rs7_SafeCopy(o, obj, sizeof o)) {
        return;
    }
    if (o[kObjFinished]) {
        return;
    }
    if (!o[kObjSingle] && !o[kObjChord]) {
        return;
    }

    const float t = Get<float>(o, kObjTime);
    if (IsScheduled(obj, t)) {
        return;
    }

    if (t - time > *maxAhead) {
        *maxAhead = t - time;
    }
    Event ev;
    if (!BuildEvent(obj, o, &ev)) {
        return;
    }

    ev.song = g_vg.song;
    const double onset = double(t) - g_vg.cfg.leadMs / 1000.0;

    ev.state = onset < double(time) - 0.05 ? kSkipped : kPending;
    if (ev.state == kSkipped && ev.voices) {
        ++g_vg.lateNotes;
    }
    Schedule(ev);
}
}  // namespace

void TickImpl(void* session, float time) {
    if (!TrackSession(session, time)) {
        return;
    }

    // Live note objects -> schedule.
    uintptr_t range[2] = {};
    if (!Rs7_SafeCopy(range, static_cast<uint8_t*>(session) + kSessNotesBegin, sizeof range)) {
        return;
    }

    const size_t count = range[1] >= range[0] ? (range[1] - range[0]) / 4 : 0;
    // (An empty list is no song end: charts have rests. The summary comes from the watchdog or the
    // next session.)
    if (count == 0 || count > kMaxNotes) {
        return;
    }
    if (!Rs7_SafeCopy(g_vg.notePtrs, reinterpret_cast<const void*>(range[0]), count * 4)) {
        return;
    }

    PruneSchedule(time);
    float maxAhead = 0;

    for (size_t i = 0; i < count; ++i) {
        ScheduleNote(g_vg.notePtrs[i], time, &maxAhead);
    }

    if (!g_vg.aheadLogged && maxAhead > 0) {
        g_vg.aheadLogged = true;
        Rs7Log("vguitar: song %d: live note list reaches %.2f s ahead at t=%.3f (%u objects)", g_vg.song,
               double(maxAhead), double(time), unsigned(count));
    }
}

}  // namespace rs7vg
