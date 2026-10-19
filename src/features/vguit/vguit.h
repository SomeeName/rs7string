#pragma once
#include "features/vguit/synth.h"
#include <cstdint>
#include <string>

// Virtual guitarist test mode (docs/ARCHITECTURE.md, level 2 "audio in-process").
// Off by default; rs7string.ini:
//
//   [test]
//   vguitar=0|1            1: write the sites TICK, JUD1, JUD2, AUD1 (else they stay unwritten).
//   vguitar_mode=chart     what to play for each chart note:
//                            chart    the note's pitch from our own formula (string 0-5: stock
//                                     open MIDI 40 45 50 55 59 64 + tuning + fret; string 6:
//                                     35 + t6 + fret) - a guitarist reading the chart
//                            game     the game's own expected MIDI (note object +58h)
//                            midi:N   MIDI N for every note (negative control, e.g. midi:36)
//                            off      play nothing (report only)
//   vguitar_filter=all     which notes to play: all | <strings> (e.g. 6, 056) | <strings>:<fret>
//                          (e.g. 6:0 = only open string-6 notes). Chords pass with "all" only.
//   vguitar_lead_ms=0      the onset is placed lead_ms before the note time (-500..500)
//   vguitar_gain=0.3       peak level of a fresh pluck, 0..1 (the game also plays it through the
//                          amp tone: keep it moderate)
//   vguitar_mix=0          0: replace the input buffer while a song runs; 1: add to it
//   vguitar_trace=0        1: log the detector's current frame for 0.6 s after each onset (diagnostic)
//   vguitar_chord6=play    the string-6 note of a chord with a string-6 column (M4, features/chords/chords.h):
//                            play     play it with the other chord notes (a guitarist reading the chart)
//                            omit     leave it out (negative control: the chord must miss)
//                            only     play only it, no native chord notes (negative control)
//                            +N / -N  play it N semitones off (negative control: wrong low note)
//                            omitlow  every chord (any arrangement) without its lowest-pitched note
//                            omithigh every chord without its highest-pitched note (stock baselines)
// vguitar is read once at startup (it decides whether the sites are written); the other keys are
// re-read at every song start (a new play session), so they can be changed between songs.
//
// Report (every finished note, JUD1 + JUD2): one rs7string.log line and one row in
// rs7string_vguitar.csv next to the DLL; a summary per song (hits / misses / accuracy / score,
// per string, timing offsets, the game's own counters).
//
// Threads: TICK / JUD1 / JUD2 on the game thread, AUD1 on the Wwise audio thread, a watchdog
// thread for the end-of-song summary. Every entry point is SEH-guarded (game memory).
//
// Files: config.cpp (option parsing), plan.cpp (what to play for a chart note), report.cpp (records, CSV, summary),
// player.cpp (the voice pool of the audio thread), synth.cpp (one plucked-string voice): logic, tests/vguit.
// hooks.cpp (the stubs, the guarded entries, startup and ini), hooks_session.cpp (TICK: the live note list ->
// schedule), hooks_report.cpp (JUD1 / JUD2: the per-note report, the watchdog), hooks_audio.cpp (AUD1: the input
// buffer): glue, sharing hooks.h (the feature's state). registration.cpp: the hook table.

// ---- startup and entry points (hooks.cpp)

// Set by Rs7Vg_StartFromIni (startup, before patching): 1 = the stubs call in.
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_on;
// Ref sites (never written), bound by Rs7_Startup: &slot -> input id table (0x13577B0, X_VGSLOT),
// &the DetectInput manager global (0x135F57C, X_VGDI).
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_slotmap;
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_dimgr;

// Startup: reads [test]; with vguitar=1 truncates rs7string_vguitar.csv (next to the ini), starts the watchdog and
// sets g_rs7_vg_on.
void Rs7Vg_StartFromIni(const wchar_t* iniPath);
bool Rs7Vg_Enabled();
// S4 (song setup): the SNG tuning of this session (independent expected MIDI).
void Rs7Vg_OnSongTuning(void* session, const int16_t* tuning, int count);

// Stub entry points (cdecl; the stubs save the FPU/SSE state around them).
extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnTick(void* session, float songTime);        // TICK 0x7E2880 entry
extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnJudge(void* note, void* session);           // JUD1 0x7E2CFF
extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnScore(int hit, void* session, void* note);  // JUD2 0x7E6AD0 entry
extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnInput(void* device, void* akBuffer);        // AUD1 0xC4618C

// The feature's hooks (registration.cpp), consumed by src/registry.cpp.
struct Rs7HookTable;
extern const Rs7HookTable kRs7VguitHooks;

namespace rs7vg {

// ---- options (config.cpp; LoadConfig, the ini reads, in hooks.cpp)

enum class Mode : uint8_t { Off, Chart, Game, Midi };
enum class Chord6 : uint8_t { Play, Omit, Only, Shift, OmitLow, OmitHigh };

struct Config {
    bool on = false;
    Mode mode = Mode::Chart;
    int midi = -1;              // Mode::Midi
    uint8_t stringMask = 0x7F;  // bit s = play string s
    int fret = -1;              // -1 = any fret
    bool filterAll = true;      // "all": chords too
    float leadMs = 0;
    float gain = 0.3f;
    bool mix = false;
    bool trace = false;            // vguitar_trace=1: log the detector frame after each onset
    Chord6 chord6 = Chord6::Play;  // vguitar_chord6
    int chord6Shift = 0;           // Chord6::Shift: semitones
    // As read (log).
    std::wstring modeRaw, filterRaw, leadRaw, gainRaw, chord6Raw;
    bool modeValid = true, filterValid = true, leadValid = true, gainValid = true, chord6Valid = true;
};

Config LoadConfig(const wchar_t* iniPath);
bool ParseMode(const wchar_t* text, Mode* mode, int* midi);
bool ParseFilter(const wchar_t* text, uint8_t* mask, int* fret, bool* all);
bool ParseFloat(const wchar_t* text, float lo, float hi, float* out);
bool ParseChord6(const wchar_t* text, Chord6* mode, int* shift);
const char* Chord6Name(Chord6 m);
const char* ModeName(Mode m);

// ---- what to play for a chart note (plan.cpp)

// Expected MIDI from our own formula: guitar strings 0-5 = 40 45 50 55 59 64 + tuning[s] + fret,
// string 6 = 35 + tuning[6] + fret (tuning[s] = 0 when the vector is shorter). -1 if out of range.
int OurMidi(int string, int fret, const int16_t* tuning, int tuningCount);
// The filter for a single note (chords: filterAll only).
bool PassesFilter(const Config& c, int string, int fret);

// The session's SNG tuning (S4), entries past count are 0.
struct Tuning {
    int16_t v[8] = {};
    int count = 0;
};

// A chord's columns (strings 0-7): fret (-1 = unused) and the template's expected MIDI.
struct Columns {
    int fret[8];
    int midi[8];
};

// The SNG note fields a plan reads (times in song seconds, as in the SNG).
struct SngNote {
    int string = -1, fret = -1;
    float sustain = 0;
    uint32_t bendCount = 0;  // as stored; the curve below holds its first 32 points
    float bendTime[32] = {};
    float bendStep[32] = {};
    float maxBend = 0;
    int slideTo = -1, slideUnpitchedTo = -1;
    bool vibrato = false;
};

// One chart note in the schedule: what the audio thread plays at its onset.
enum EventState : uint8_t { kFree = 0, kPending = 1, kStarted = 2, kSkipped = 3 };

struct Event {
    uint8_t state = kFree;
    const void* obj = nullptr;
    float time = 0;
    float sustain = 0;
    int string = -1, fret = -1;
    bool chord = false;
    int voices = 0;
    float midi[7] = {};
    int s6Played = -1;  // chord: the MIDI played for its string-6 column
    Glide glide;
    double onset = 0;  // song time of the onset we played (audio thread)
    int song = 0;
};

// Fills e's note fields and voices (e->obj / time / chord already set). chord: its columns, nullptr if unreadable
// (nothing played). gameMidi: the note object's expected MIDI (+58h), for Mode::Game.
void PlanEvent(const Config& c, const Tuning& t, const SngNote& note, const Columns* chord, int gameMidi, Event* e);

// ---- report (report.cpp)

// One finished note as reported.
struct Record {
    int song = 0, idx = 0;
    float time = 0;
    int string = -1, fret = -1;
    bool chord = false;
    int gameMidi = -1, ourMidi = -1;
    int played = -1;          // MIDI we synthesized (first voice of a chord), -1 = not played
    double onset = 0;         // song time of our onset (our clock mapping), 0 = not played
    int detected = 0;         // note +C1h
    float hitTime = 0;        // note +C4h
    int attempts = 0;         // note +174h
    int scored = -1;          // JUD2 CL: 1 hit, 0 miss, -1 not scored
    bool judged = false;      // came through JUD1
    float fwd = 0, back = 0;  // note +94h / +98h
    float sustain = 0;
    uint32_t mask = 0;  // SNG note mask
    int s6Fret = -1;    // chord: the template's string-6 column (features/chords/chords.h), -1 = none
    int s6Midi = -1;    // its expected MIDI (35 + t6 + fret)
    int s6Played = -1;  // the MIDI we played for it, -1 = not played
};

// A chord's record from its columns: the lowest used string 0-5 gives string / fret / game MIDI; a string-6 column
// (7-string tuning) gives s6Fret / s6Midi.
void RecordChord(const Columns& col, const Tuning& t, Record* r);

std::string CsvHeader();
std::string CsvRow(const Record& r);
std::string LogLine(const Record& r);
// A note counts as hit when JUD2 scored it 1, or (not scored) when JUD1 saw it detected.
bool IsHit(const Record& r);

struct Counters {  // the session's floats (Play_Session_ScoreNote 0x7E6AD0)
    bool valid = false;
    float singleAcc = 0, singleHit = 0, singleMiss = 0;  // +428 / +42C / +430
    float totalAcc = 0, totalHit = 0, totalMiss = 0;     // +43C / +440 / +444
    double score = 0;                                    // +448
    uint32_t scoreShown = 0;                             // +450 (raw dword)
};

struct Totals {
    int notes = 0, chords = 0, hits = 0, misses = 0;
    int played = 0, playedHits = 0, unplayedHits = 0;
    int perString[9] = {}, perStringHits[9] = {};
    int chords6 = 0, chords6Hits = 0;  // chords with a string-6 column
    int offsetCount = 0;
    double offsetSum = 0, offsetMin = 1e9, offsetMax = -1e9;  // ms, detected notes we played
    void Add(const Record& r);
};

std::string SummaryLine(int song, const Totals& t, const Counters& c);

// ---- the audio thread's voices (player.cpp)

// An event's onset inside a buffer: sample offset `at`, `ev` its index in the events passed to Render.
struct Onset {
    int at;
    int ev;
};

// A mono guitarist: a new onset damps the voices of earlier notes; a chord is one voice per note, level
// gain / sqrt(voices); a voice holds for the note's sustain + 0.3 s.
class Player {
public:
    static constexpr int kVoices = 10;

    // Overwrites mix[0, n) with the voices, starting each onset's voices at its offset (onsets are sorted here).
    void Render(float* mix, int n, Onset* onsets, int count, const Event* events, float gain);
    void ReleaseAll();

    unsigned Started() const { return started_; }

private:
    void Start(const Event& e, float gain);

    Voice voice_[kVoices];
    float voiceTime_[kVoices] = {};  // chart time of the event that started the voice
    uint32_t seed_ = 1;
    unsigned started_ = 0;
};

}  // namespace rs7vg
