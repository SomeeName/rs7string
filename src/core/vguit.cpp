#include "core/vguit.h"
#include "core/chord7.h"
#include "core/config.h"
#include "core/eight.h"
#include "core/nslot.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <share.h>
#include <utility>

extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_on = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_slotmap = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_dimgr = 0;
extern "C" uintptr_t g_rs7_frame_global;  // X_DT: &0x135FBFC (song clock at [it]+0D8h)

namespace rs7vg {

// ---------------------------------------------------------------- config (pure)

const char* ModeName(Mode m) {
    switch (m) {
        case Mode::Off:
            return "off";
        case Mode::Chart:
            return "chart";
        case Mode::Game:
            return "game";
        case Mode::Midi:
            return "midi";
    }

    return "?";
}

namespace {
std::wstring Trim(const wchar_t* t) {
    while (*t && std::iswspace(*t)) {
        ++t;
    }

    std::wstring s(t);

    while (!s.empty() && std::iswspace(s.back())) {
        s.pop_back();
    }

    for (auto& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }

    return s;
}
}  // namespace

bool ParseMode(const wchar_t* text, Mode* mode, int* midi) {
    const std::wstring s = Trim(text);

    if (s == L"chart") {
        *mode = Mode::Chart;
        return true;
    }

    if (s == L"game") {
        *mode = Mode::Game;
        return true;
    }

    if (s == L"off") {
        *mode = Mode::Off;
        return true;
    }

    if (s.compare(0, 5, L"midi:") == 0 && s.size() > 5) {
        wchar_t* end = nullptr;
        const long v = std::wcstol(s.c_str() + 5, &end, 10);
        if (*end || v < 20 || v > 100) {
            return false;
        }

        *mode = Mode::Midi;
        *midi = static_cast<int>(v);
        return true;
    }

    return false;
}

bool ParseFilter(const wchar_t* text, uint8_t* mask, int* fret, bool* all) {
    const std::wstring s = Trim(text);

    if (s == L"all") {
        *mask = g_rs7_eight ? 0xFF : 0x7F;
        *fret = -1;
        *all = true;
        return true;
    }

    const size_t colon = s.find(L':');
    const std::wstring strings = s.substr(0, colon);
    if (strings.empty()) {
        return false;
    }

    uint8_t m = 0;

    for (wchar_t ch : strings) {
        if (ch < L'0' || ch > (g_rs7_eight ? L'7' : L'6')) {
            return false;
        }

        m = static_cast<uint8_t>(m | (1u << (ch - L'0')));
    }

    int f = -1;

    if (colon != std::wstring::npos) {
        wchar_t* end = nullptr;
        const std::wstring ft = s.substr(colon + 1);
        if (ft.empty()) {
            return false;
        }

        const long v = std::wcstol(ft.c_str(), &end, 10);
        if (*end || v < 0 || v > 30) {
            return false;
        }

        f = static_cast<int>(v);
    }

    *mask = m;
    *fret = f;
    *all = false;

    return true;
}

bool ParseFloat(const wchar_t* text, float lo, float hi, float* out) {
    while (*text && std::iswspace(*text)) {
        ++text;
    }

    if (!*text) {
        return false;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text) {
        return false;
    }

    while (*end && std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < lo || v > hi) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}

const char* Chord6Name(Chord6 m) {
    switch (m) {
        case Chord6::Play:
            return "play";
        case Chord6::Omit:
            return "omit";
        case Chord6::Only:
            return "only";
        case Chord6::Shift:
            return "shift";
        case Chord6::OmitLow:
            return "omitlow";
        case Chord6::OmitHigh:
            return "omithigh";
    }

    return "?";
}

bool ParseChord6(const wchar_t* text, Chord6* mode, int* shift) {
    const std::wstring s = Trim(text);

    if (s == L"play") {
        *mode = Chord6::Play;
        *shift = 0;
        return true;
    }

    if (s == L"omit") {
        *mode = Chord6::Omit;
        *shift = 0;
        return true;
    }

    if (s == L"only") {
        *mode = Chord6::Only;
        *shift = 0;
        return true;
    }

    if (s == L"omitlow") {
        *mode = Chord6::OmitLow;
        *shift = 0;
        return true;
    }

    if (s == L"omithigh") {
        *mode = Chord6::OmitHigh;
        *shift = 0;
        return true;
    }

    if (s.size() >= 2 && (s[0] == L'+' || s[0] == L'-')) {
        wchar_t* end = nullptr;
        const long v = std::wcstol(s.c_str(), &end, 10);
        if (*end || v == 0 || v < -24 || v > 24) {
            return false;
        }

        *mode = Chord6::Shift;
        *shift = static_cast<int>(v);
        return true;
    }

    return false;
}

Config LoadConfig(const wchar_t* ini) {
    Config c;

    if (g_rs7_eight) {
        c.stringMask = 0xFF;
    }
    c.on = Rs7Config_ReadInt(L"test", L"vguitar", 0, ini) != 0;
    wchar_t buf[128];

    GetPrivateProfileStringW(L"test", L"vguitar_mode", L"", buf, 128, ini);
    c.modeRaw = buf;
    if (!c.modeRaw.empty()) {
        c.modeValid = ParseMode(buf, &c.mode, &c.midi);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_filter", L"", buf, 128, ini);
    c.filterRaw = buf;
    if (!c.filterRaw.empty()) {
        c.filterValid = ParseFilter(buf, &c.stringMask, &c.fret, &c.filterAll);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_lead_ms", L"", buf, 128, ini);
    c.leadRaw = buf;
    if (!c.leadRaw.empty()) {
        c.leadValid = ParseFloat(buf, -500, 500, &c.leadMs);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_gain", L"", buf, 128, ini);
    c.gainRaw = buf;
    if (!c.gainRaw.empty()) {
        c.gainValid = ParseFloat(buf, 0, 1, &c.gain);
    }
    c.mix = Rs7Config_ReadInt(L"test", L"vguitar_mix", 0, ini) != 0;
    c.trace = Rs7Config_ReadInt(L"test", L"vguitar_trace", 0, ini) != 0;
    GetPrivateProfileStringW(L"test", L"vguitar_chord6", L"", buf, 128, ini);
    c.chord6Raw = buf;
    if (!c.chord6Raw.empty()) {
        c.chord6Valid = ParseChord6(buf, &c.chord6, &c.chord6Shift);
    }

    return c;
}

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

// ---------------------------------------------------------------- report (pure)

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

// ---------------------------------------------------------------- game side

namespace {
using namespace rs7vg;

// Layouts (virtual-guitarist.md sections 6/7, data.md).
constexpr uint32_t kSessInput = 0x1C, kSessNotesBegin = 0x41C, kSessNotesEnd = 0x420;
constexpr uint32_t kObjSng = 0x2C, kObjTemplate = 0x30, kObjTime = 0x38, kObjFlag = 0x50, kObjSingle = 0x51,
                   kObjChord = 0x52, kObjMidi = 0x58, kObjFwd = 0x94, kObjBack = 0x98, kObjDetected = 0xC1,
                   kObjFinished = 0xC3, kObjHitTime = 0xC4, kObjAttempts = 0x174, kObjSize = 0x178;
constexpr uint32_t kSngMask = 0x00, kSngString = 0x10, kSngFret = 0x11, kSngSlideTo = 0x34, kSngSlideUnp = 0x35,
                   kSngVibrato = 0x3A, kSngSustain = 0x3C, kSngMaxBend = 0x40, kSngBend = 0x44, kSngBendCount = 0x1C4,
                   kSngSize = 0x1C8;
constexpr uint32_t kTplFrets = 0x04, kTplNotes = 0x10, kTplSize = 0x48;

// A chord's columns (strings 0-7) from its template: the T8 record's N-slot arrays (core/nslot.h; every
// template pointer at play time is one), else the six stock columns.
struct Columns {
    int fret[rs7ns::kSlots];
    int midi[rs7ns::kSlots];
};

bool SafeCopy(void* dst, const void* src, size_t n);

bool ReadColumns(const void* tpl, Columns* c) {
    for (int s = 0; s < rs7ns::kSlots; ++s) {
        c->fret[s] = c->midi[s] = -1;
    }

    uint8_t t[rs7ns::kT8Size];
    const bool t8 = Rs7Ns_AsT8(tpl) != nullptr;
    if (!SafeCopy(t, tpl, t8 ? rs7ns::kT8Size : kTplSize)) {
        return false;
    }

    const uint32_t frets = t8 ? rs7ns::kT8Frets : kTplFrets, notes = t8 ? rs7ns::kT8Midi : kTplNotes;

    for (int s = 0; s < (t8 ? rs7ns::kSlots : 6); ++s) {
        c->fret[s] = static_cast<int8_t>(t[frets + s]);
        std::memcpy(&c->midi[s], t + notes + 4 * s, 4);
    }

    return true;
}

bool SafeCopy(void* dst, const void* src, size_t n) {
    if (!src) {
        return false;
    }

    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
T Get(const uint8_t* b, uint32_t off) {
    T v;

    std::memcpy(&v, b + off, sizeof v);

    return v;
}

template <class T>
bool ReadAt(uintptr_t addr, T* out) {
    return SafeCopy(out, reinterpret_cast<const void*>(addr), sizeof(T));
}

// ---- state
Config g_cfg;
std::wstring g_csvPath;
std::wstring g_iniPath;
FILE* g_csv = nullptr;
CRITICAL_SECTION g_cs;               // report state (game thread + watchdog)
SRWLOCK g_schedLock = SRWLOCK_INIT;  // schedule (game thread + audio thread)

// Tuning per session (S4).
void* g_tuneSession = nullptr;
int16_t g_tuning[8] = {};
int g_tuningCount = 0;

// Session tracking (game thread).
void* g_session = nullptr;
volatile LONG g_sessionInput = -1;  // session+1Ch, read by the audio thread
volatile LONG g_lastTickMs = 0;
DWORD g_ownTickMs = 0;
volatile LONG g_active = 0;  // a session ticked within the last second (audio thread reads)
float g_lastTime = 0;
int g_song = 0;
int g_idx = 0;
bool g_aheadLogged = false;
int g_lateNotes = 0;

// Report state (g_cs).
Record g_pending;
const void* g_pendingObj = nullptr;
bool g_hasPending = false;
Totals g_totals;
Counters g_counters;
int g_sinceSummary = 0;

// Schedule (g_schedLock).
enum : uint8_t { kFree = 0, kPending = 1, kStarted = 2, kSkipped = 3 };

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

constexpr int kEvents = 256;
Event g_ev[kEvents];
volatile LONG g_flush = 0;  // game thread asks the audio thread to drop its voices (time jump / new song)

// Audio thread only.
constexpr int kVoices = 10;
Voice g_voice[kVoices];
float g_voiceTime[kVoices];  // chart time of the event that started the voice
uint32_t g_seed = 1;
double g_lastBase = -1;
unsigned g_buffers = 0;
unsigned g_started = 0;
bool g_formatLogged = false;
bool g_badFormat = false;
DWORD g_lastStatMs = 0;
int g_clockFallbacks = 0;
float g_mixBuf[8192];
double g_traceUntil = -1;
void TraceDetector(int inputId, double base, double rms);

bool g_csvWarned = false;

void CsvWrite(const std::string& s) {
    if (!g_csv) {
        if (!g_csvWarned) {
            Rs7Log("vguitar: CSV %ls not open", g_csvPath.c_str());
        }
        g_csvWarned = true;
        return;
    }

    std::fputs(s.c_str(), g_csv);
    std::fflush(g_csv);
}

void ReadCounters(void* session, Counters* c) {
    uint8_t b[0x60];
    if (!SafeCopy(b, static_cast<uint8_t*>(session) + 0x420, sizeof b)) {
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

// g_cs held.
void Emit(const Record& r) {
    Rs7Log("%s", LogLine(r).c_str());
    CsvWrite(CsvRow(r));
    g_totals.Add(r);
    ++g_sinceSummary;
}

// g_cs held.
void Summary(const char* why) {
    if (g_hasPending) {
        Emit(g_pending);
        g_hasPending = false;
    }

    if (!g_sinceSummary) {
        return;
    }

    Rs7Log("%s (%s)", SummaryLine(g_song, g_totals, g_counters).c_str(), why);
    if (g_lateNotes) {
        Rs7Log("vguitar: song %d: %d notes first seen too late to play (onset already past)", g_song, g_lateNotes);
    }
    char b[64];

    std::snprintf(b, sizeof b, "# summary song %d (%s)\n", g_song, why);
    CsvWrite(b);
    g_sinceSummary = 0;
}

// The record of a finished note (game thread).
bool BuildRecord(const void* obj, Record* r) {
    uint8_t o[kObjSize];
    if (!SafeCopy(o, obj, sizeof o)) {
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

    if (SafeCopy(n, Get<const void*>(o, kObjSng), sizeof n)) {
        r->mask = Get<uint32_t>(n, kSngMask);
        r->string = n[kSngString];
        r->fret = static_cast<int8_t>(n[kSngFret]);
        r->sustain = Get<float>(n, kSngSustain);
    }

    if (r->chord) {
        Columns col;

        if (ReadColumns(Get<const void*>(o, kObjTemplate), &col)) {
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

            if (g_tuningCount == 7 && col.fret[6] >= 0) {
                r->s6Fret = col.fret[6];
                r->s6Midi = OurMidi(6, col.fret[6], g_tuning, g_tuningCount);
            }
        }
    }

    r->ourMidi = OurMidi(r->string, r->fret, g_tuning, g_tuningCount);
    // What we played for it.
    AcquireSRWLockShared(&g_schedLock);

    for (const Event& e : g_ev) {
        if (e.state == kStarted && e.voices > 0 && e.obj == obj && e.time == r->time) {
            r->played = static_cast<int>(std::lround(e.midi[0]));
            r->onset = e.onset;
            r->s6Played = e.s6Played;
            break;
        }
    }

    ReleaseSRWLockShared(&g_schedLock);

    return true;
}

// A new event from a live note object (game thread, schedule lock NOT held).
bool BuildEvent(const void* obj, const uint8_t* o, Event* e) {
    e->obj = obj;
    e->time = Get<float>(o, kObjTime);
    e->chord = o[kObjChord] != 0;
    uint8_t n[kSngSize];
    if (!SafeCopy(n, Get<const void*>(o, kObjSng), sizeof n)) {
        return false;
    }

    e->string = n[kSngString];
    e->fret = static_cast<int8_t>(n[kSngFret]);
    e->sustain = Get<float>(n, kSngSustain);
    if (!(e->sustain >= 0 && e->sustain < 60)) {
        e->sustain = 0;
    }
    e->voices = 0;
    e->s6Played = -1;
    if (g_cfg.mode == Mode::Off) {
        return true;
    }

    if (e->chord) {
        if (!g_cfg.filterAll) {
            return true;
        }

        Columns col;
        if (!ReadColumns(Get<const void*>(o, kObjTemplate), &col)) {
            return true;
        }

        const bool has6 = g_tuningCount == 7 && col.fret[6] >= 0;

        for (int s = 0; s < 6 && !(has6 && g_cfg.chord6 == Chord6::Only); ++s) {
            const int f = col.fret[s];
            if (f < 0) {
                continue;
            }

            int m = g_cfg.mode == Mode::Game ? col.midi[s] : OurMidi(s, f, g_tuning, g_tuningCount);

            if (g_cfg.mode == Mode::Midi) {
                m = g_cfg.midi;
            }
            if (m > 0 && m < 128 && e->voices < 7) {
                e->midi[e->voices++] = float(m);
            }
            if (g_cfg.mode == Mode::Midi) {
                return true;
            }
        }

        if (has6 && g_cfg.chord6 != Chord6::Omit) {  // the 7th string's column (T8 slot 6)
            int m = g_cfg.mode == Mode::Game ? col.midi[6] : OurMidi(6, col.fret[6], g_tuning, g_tuningCount);

            if (g_cfg.mode == Mode::Midi) {
                m = g_cfg.midi;
            }
            if (g_cfg.chord6 == Chord6::Shift) {
                m += g_cfg.chord6Shift;
            }

            if (m > 0 && m < 128 && e->voices < 7) {
                e->midi[e->voices++] = float(m);
                e->s6Played = m;
            }
        }

        // Negative controls for any chord: leave out its lowest / highest-pitched note.
        if ((g_cfg.chord6 == Chord6::OmitLow || g_cfg.chord6 == Chord6::OmitHigh) && e->voices > 1) {
            int k = 0;

            for (int v = 1; v < e->voices; ++v) {
                if (g_cfg.chord6 == Chord6::OmitLow ? e->midi[v] < e->midi[k] : e->midi[v] > e->midi[k]) {
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

        return true;
    }

    if (!PassesFilter(g_cfg, e->string, e->fret)) {
        return true;
    }

    int m = -1;

    switch (g_cfg.mode) {
        case Mode::Chart:
            m = OurMidi(e->string, e->fret, g_tuning, g_tuningCount);
            break;
        case Mode::Game:
            m = Get<int32_t>(o, kObjMidi);
            break;
        case Mode::Midi:
            m = g_cfg.midi;
            break;
        case Mode::Off:
            break;
    }

    if (m <= 0 || m >= 128) {
        return true;
    }

    e->midi[0] = float(m);
    e->voices = 1;
    // Techniques as pitch glides (seconds after the onset).
    Glide& g = e->glide;

    g = Glide{};
    const uint32_t bends = Get<uint32_t>(n, kSngBendCount);

    if (bends > 0 && bends <= 32) {
        for (uint32_t i = 0; i < bends; ++i) {
            g.bendT[i] = Get<float>(n, kSngBend + i * 0xC) - e->time;
            g.bendStep[i] = Get<float>(n, kSngBend + i * 0xC + 4);
        }

        g.bendCount = static_cast<int>(bends);
    } else {
        const float maxBend = Get<float>(n, kSngMaxBend);

        if (maxBend > 0 && maxBend < 5 && e->sustain > 0) {  // no curve: rise over the first third
            g.bendT[0] = e->sustain / 3;
            g.bendStep[0] = maxBend;
            g.bendCount = 1;
        }
    }

    const int slideTo = static_cast<int8_t>(n[kSngSlideTo]), slideUnp = static_cast<int8_t>(n[kSngSlideUnp]);
    const int target = slideTo >= 0 ? slideTo : slideUnp;

    if (target >= 0 && e->sustain > 0 && e->fret >= 0) {
        g.slideSemis = float(target - e->fret);
        g.slideStart = 0;
        g.slideEnd = e->sustain;
    }

    if (n[kSngVibrato] != 0) {
        g.vibratoSemis = 0.3f;
    }

    return true;
}

// g_cs held, game thread.
void NewSession(void* session) {
    Summary("next session");

    {  // every [test] key but vguitar itself is re-read at each song start
        Config c = LoadConfig(g_iniPath.c_str());

        c.on = g_cfg.on;
        g_cfg = c;
    }

    g_session = session;
    ++g_song;
    g_idx = 0;
    g_totals = Totals{};
    g_counters = Counters{};
    g_aheadLogged = false;
    g_lateNotes = 0;
    int32_t input = -1;

    ReadAt(reinterpret_cast<uintptr_t>(session) + kSessInput, &input);
    InterlockedExchange(&g_sessionInput, input);
    AcquireSRWLockExclusive(&g_schedLock);

    for (Event& e : g_ev) {
        e.state = kFree;
    }

    ReleaseSRWLockExclusive(&g_schedLock);
    InterlockedExchange(&g_flush, 1);
    const bool tuned = g_tuneSession == session;

    Rs7Log("vguitar: song %d session %p input %d, tuning %s (%d entries: %d %d %d %d %d %d %d - unused entries 0), "
           "mode %s%s, filter %ls, "
           "lead %.1f ms, gain %.2f, %s, chord6 %s %d",
           g_song, session, input, tuned ? "from S4" : "NOT SEEN (E standard assumed)", g_tuningCount, g_tuning[0],
           g_tuning[1], g_tuning[2], g_tuning[3], g_tuning[4], g_tuning[5], g_tuning[6], ModeName(g_cfg.mode),
           g_cfg.mode == Mode::Midi ? (" " + std::to_string(g_cfg.midi)).c_str() : "",
           g_cfg.filterAll ? L"all" : g_cfg.filterRaw.c_str(), double(g_cfg.leadMs), double(g_cfg.gain),
           g_cfg.mix ? "mix" : "replace", Chord6Name(g_cfg.chord6), g_cfg.chord6Shift);
    if (!tuned) {
        g_tuningCount = 0;
    }
    float clock = 0;
    uintptr_t fg = 0;

    if (g_rs7_frame_global && ReadAt(g_rs7_frame_global, &fg) && fg) {
        ReadAt(fg + 0xD8, &clock);
    }
    Rs7Log("vguitar: song %d start: master clock %.3f", g_song, double(clock));
}

void TickImpl(void* session, float time) {
    EnterCriticalSection(&g_cs);

    if (session != g_session) {
        // Another session while ours still ticks (multiplayer): keep the first one.
        if (g_session && GetTickCount() - g_ownTickMs < 1000) {
            LeaveCriticalSection(&g_cs);
            return;
        }

        NewSession(session);
    }

    g_ownTickMs = GetTickCount();
    InterlockedExchange(&g_lastTickMs, LONG(g_ownTickMs));
    InterlockedExchange(&g_active, 1);

    if (time < 0.5f && g_lastTime > 5.0f) {
        // The same session restarted from the top (results screen -> play again): a new run with its
        // own summary and freshly read [test] keys.
        Rs7Log("vguitar: song restarted (%.3f -> %.3f): new run", double(g_lastTime), double(time));
        NewSession(session);
    } else if (time < g_lastTime - 0.5f) {
        Rs7Log("vguitar: song time went back %.3f -> %.3f: voices and schedule reset", double(g_lastTime),
               double(time));
        AcquireSRWLockExclusive(&g_schedLock);

        for (Event& e : g_ev) {
            if (e.state == kPending) {
                e.state = kFree;
            }
        }

        ReleaseSRWLockExclusive(&g_schedLock);
        InterlockedExchange(&g_flush, 1);
    }

    g_lastTime = time;
    ReadCounters(session, &g_counters);
    LeaveCriticalSection(&g_cs);

    // Live note objects -> schedule.
    uintptr_t range[2] = {};
    if (!SafeCopy(range, static_cast<uint8_t*>(session) + kSessNotesBegin, sizeof range)) {
        return;
    }

    const size_t count = range[1] >= range[0] ? (range[1] - range[0]) / 4 : 0;
    // (An empty list is no song end: charts have rests. The summary comes from the watchdog or the
    // next session.)
    if (count == 0 || count > 2048) {
        return;
    }

    static const void* ptrs[2048];
    if (!SafeCopy(ptrs, reinterpret_cast<const void*>(range[0]), count * 4)) {
        return;
    }

    AcquireSRWLockExclusive(&g_schedLock);

    // Prune.
    for (Event& e : g_ev) {
        if (e.state == kFree) {
            continue;
        }

        const float end = e.time + e.sustain + 2.0f;

        if (end < time) {
            e.state = kFree;
        }
    }

    ReleaseSRWLockExclusive(&g_schedLock);
    float maxAhead = 0;

    for (size_t i = 0; i < count; ++i) {
        const void* obj = ptrs[i];
        uint8_t o[kObjSize];
        if (!obj || !SafeCopy(o, obj, sizeof o)) {
            continue;
        }
        if (o[kObjFinished]) {
            continue;
        }
        if (!o[kObjSingle] && !o[kObjChord]) {
            continue;
        }

        const float t = Get<float>(o, kObjTime);
        bool known = false;

        AcquireSRWLockShared(&g_schedLock);

        for (const Event& e : g_ev) {
            if (e.state != kFree && e.obj == obj && e.time == t) {
                known = true;
                break;
            }
        }

        ReleaseSRWLockShared(&g_schedLock);
        if (known) {
            continue;
        }

        if (t - time > maxAhead) {
            maxAhead = t - time;
        }
        Event ev;
        if (!BuildEvent(obj, o, &ev)) {
            continue;
        }

        ev.song = g_song;
        const double onset = double(t) - g_cfg.leadMs / 1000.0;

        ev.state = onset < double(time) - 0.05 ? kSkipped : kPending;
        if (ev.state == kSkipped && ev.voices) {
            ++g_lateNotes;
        }
        AcquireSRWLockExclusive(&g_schedLock);
        Event* slot = nullptr;

        for (Event& e : g_ev) {
            if (e.state == kFree) {
                slot = &e;
                break;
            }
        }

        if (slot) {
            *slot = ev;
        }
        ReleaseSRWLockExclusive(&g_schedLock);

        if (!slot) {
            static bool full = false;

            if (!full) {
                Rs7Log("vguitar: schedule full (%d events)", kEvents);
            }
            full = true;
        }
    }

    if (!g_aheadLogged && maxAhead > 0) {
        g_aheadLogged = true;
        Rs7Log("vguitar: song %d: live note list reaches %.2f s ahead at t=%.3f (%u objects)", g_song, double(maxAhead),
               double(time), unsigned(count));
    }
}

void JudgeImpl(void* note, void* session) {
    Record r;
    if (!BuildRecord(note, &r)) {
        return;
    }

    r.judged = true;
    EnterCriticalSection(&g_cs);

    if (session != g_session) {  // JUD1 runs inside TICK's function: only another player's session
        LeaveCriticalSection(&g_cs);
        return;
    }

    if (g_hasPending) {
        Emit(g_pending);
    }
    r.song = g_song;
    r.idx = ++g_idx;
    g_pending = r;
    g_pendingObj = note;
    g_hasPending = true;
    LeaveCriticalSection(&g_cs);
}

void ScoreImpl(int hit, void* session, void* note) {
    if (session != g_session) {
        return;  // not the session TICK locked onto (game thread: no race)
    }

    float t = 0;

    ReadAt(reinterpret_cast<uintptr_t>(note) + kObjTime, &t);
    EnterCriticalSection(&g_cs);

    if (g_hasPending && g_pendingObj == note && g_pending.time == t) {
        g_pending.scored = hit;
        ReadCounters(session, &g_counters);
        Emit(g_pending);
        g_hasPending = false;
        LeaveCriticalSection(&g_cs);
        return;
    }

    LeaveCriticalSection(&g_cs);
    Record r;  // scored without JUD1 (Play_Session_MissPendingNotes 0x7E2861)
    if (!BuildRecord(note, &r)) {
        return;
    }

    r.scored = hit;
    EnterCriticalSection(&g_cs);

    if (g_hasPending) {
        Emit(g_pending);
        g_hasPending = false;
    }

    r.song = g_song;
    r.idx = ++g_idx;
    ReadCounters(session, &g_counters);
    Emit(r);
    LeaveCriticalSection(&g_cs);
}

// Song time at the first sample of this buffer: det+D08h + (DI+30h - det+D18h) (section 2), or the
// master clock while no detector frame exists. Audio thread.
bool BufferBase(int inputId, double* base, double* master, bool* mapped) {
    *mapped = false;
    float clock = 0;
    uintptr_t fg = 0;
    if (!g_rs7_frame_global || !ReadAt(g_rs7_frame_global, &fg) || !fg || !ReadAt(fg + 0xD8, &clock)) {
        return false;
    }

    *master = clock;
    *base = clock;
    uintptr_t mgr = 0, di = 0, det = 0;
    if (!g_rs7_vg_dimgr || !ReadAt(g_rs7_vg_dimgr, &mgr) || !mgr) {
        return true;
    }
    if (inputId != 0 && inputId != 2) {
        return true;
    }
    if (!ReadAt(mgr + (inputId == 0 ? 0x10 : 0x14), &di) || !di) {
        return true;
    }
    if (!ReadAt(di + 4, &det) || !det) {
        return true;
    }

    double d08 = 0, d18 = 0, di30 = 0;
    if (!ReadAt(det + 0xD08, &d08) || !ReadAt(det + 0xD18, &d18) || !ReadAt(di + 0x30, &di30)) {
        return true;
    }
    if (d08 == 0 && d18 == 0) {
        return true;
    }

    const double b = d08 + (di30 - d18);

    if (!(std::fabs(b - clock) < 0.5)) {
        ++g_clockFallbacks;
        return true;
    }

    *base = b;
    *mapped = true;

    return true;
}

void InputImpl(void* dev, void* akbuf) {
    uint8_t* buf = static_cast<uint8_t*>(akbuf);
    uint8_t d[0x28];
    uint8_t hdr[0x10];
    if (!SafeCopy(d, dev, sizeof d) || !SafeCopy(hdr, buf, sizeof hdr)) {
        return;
    }

    float* data = Get<float*>(hdr, 0);
    const int n = Get<uint16_t>(hdr, 0xC);
    const int valid = Get<uint16_t>(hdr, 0xE);
    const int slot = Get<int32_t>(d, 0xC);
    const int align = Get<uint16_t>(d, 0x24);
    int inputId = -1;

    if (g_rs7_vg_slotmap && slot >= 0 && slot < 10) {
        ReadAt(g_rs7_vg_slotmap + 4 * uintptr_t(slot), &inputId);
    }

    if (!g_formatLogged) {
        g_formatLogged = true;
        g_badFormat = align != 4 || !data || n <= 0 || n > 8192;
        Rs7Log("vguitar: first input buffer: device %p slot %d input %d, uMaxFrames %d, uValidFrames %d, "
               "blockAlign %d, data %p%s",
               dev, slot, inputId, n, valid, align, static_cast<void*>(data),
               g_badFormat ? " -> UNEXPECTED FORMAT, nothing written" : "");
    }

    if (g_badFormat || !data || n <= 0 || n > 8192) {
        return;
    }

    if (InterlockedExchange(&g_flush, 0)) {
        for (Voice& v : g_voice) {
            v.Release();
        }

        g_lastBase = -1;
    }

    const DWORD now = GetTickCount();
    const bool active = g_active && now - DWORD(g_lastTickMs) < 1000;

    if (!active) {
        if (g_active) {
            InterlockedExchange(&g_active, 0);
        }
        return;  // menus: the real input passes through
    }

    if (inputId != g_sessionInput) {
        return;  // another player's input
    }

    double base = 0, master = 0;
    bool mapped = false;
    if (!BufferBase(inputId, &base, &master, &mapped)) {
        return;
    }

    if (g_lastBase >= 0 && base < g_lastBase - 0.25) {
        for (Voice& v : g_voice) {
            v.Release();
        }
    }

    g_lastBase = base;
    ++g_buffers;

    // Onsets in this buffer, in order.
    struct Start {
        int at;
        int ev;
    };

    Start starts[32];
    int ns = 0;
    Event local[32];
    const double lead = g_cfg.leadMs / 1000.0, end = base + double(n) / kRate;

    AcquireSRWLockExclusive(&g_schedLock);

    for (int i = 0; i < kEvents && ns < 32; ++i) {
        Event& e = g_ev[i];
        if (e.state != kPending) {
            continue;
        }

        const double onset = double(e.time) - lead;
        if (onset >= end) {
            continue;
        }

        if (onset < base - 0.05) {  // late (pause, seek, first buffer): never play it out of time
            e.state = kSkipped;
            continue;
        }

        int at = onset <= base ? 0 : static_cast<int>((onset - base) * kRate);

        if (at >= n) {
            at = n - 1;
        }
        e.state = kStarted;
        e.onset = base + double(at) / kRate;

        if (e.voices > 0) {
            starts[ns] = {at, ns};
            local[ns] = e;
            ++ns;
        }
    }

    ReleaseSRWLockExclusive(&g_schedLock);

    // Sort by offset (few entries).
    for (int a = 1; a < ns; ++a) {
        for (int b = a; b > 0 && starts[b].at < starts[b - 1].at; --b) {
            std::swap(starts[b], starts[b - 1]);
        }
    }

    float* mix = g_mixBuf;

    std::memset(mix, 0, sizeof(float) * n);
    int pos = 0;

    for (int k = 0; k <= ns; ++k) {
        const int seg = k < ns ? starts[k].at : n;

        if (seg > pos) {
            for (Voice& v : g_voice) {
                v.Render(mix + pos, seg - pos);
            }

            pos = seg;
        }

        if (k == ns) {
            break;
        }

        const Event& e = local[starts[k].ev];

        // Mono guitarist: a new onset damps the voices of earlier notes (not those of the same instant).
        for (int v = 0; v < kVoices; ++v) {
            if (g_voice[v].Active() && g_voiceTime[v] < e.time - 0.005f) {
                g_voice[v].Release();
            }
        }

        const float hold = (e.sustain > 0 ? e.sustain : 0.0f) + 0.30f;
        const float level = g_cfg.gain / (e.voices > 1 ? std::sqrt(float(e.voices)) : 1.0f);

        for (int j = 0; j < e.voices; ++j) {
            int slotV = -1;

            for (int v = 0; v < kVoices; ++v) {
                if (!g_voice[v].Active()) {
                    slotV = v;
                    break;
                }
            }

            if (slotV < 0) {  // steal the oldest
                int64_t age = -1;

                for (int v = 0; v < kVoices; ++v) {
                    if (g_voice[v].Age() > age) {
                        age = g_voice[v].Age();
                        slotV = v;
                    }
                }
            }

            g_voice[slotV].Start(e.midi[j], level, g_seed = g_seed * 1664525u + 1013904223u, e.glide);
            g_voice[slotV].SetHold(static_cast<int64_t>(hold * kRate));
            g_voiceTime[slotV] = e.time;
            ++g_started;
        }
    }

    if (g_cfg.mix) {
        for (int i = 0; i < n; ++i) {
            data[i] += mix[i];
        }
    } else {
        std::memcpy(data, mix, sizeof(float) * n);
    }

    *reinterpret_cast<uint16_t*>(buf + 0xE) = static_cast<uint16_t>(n);
    if (ns) {
        g_traceUntil = base + 0.6;
    }
    if (g_cfg.trace && base < g_traceUntil && (g_buffers % 4) == 0) {
        TraceDetector(inputId, base, Rms(mix, n));
    }

    if (now - g_lastStatMs > 5000) {
        g_lastStatMs = now;
        Rs7Log("vguitar: audio: %u buffers of %d (valid was %d), base %.4f %s master %.4f (diff %+.1f ms), "
               "%u voices started, %d clock fallbacks, rms %.4f",
               g_buffers, n, valid, base, mapped ? "(detector map)" : "(master)", master, (base - master) * 1000.0,
               g_started, g_clockFallbacks, Rms(mix, n));
    }
}

// [test] vguitar_trace=1: the detector's current frame (det+5E8h + frame offsets of section 4),
// every 4th buffer for 0.6 s after each onset. Audio thread; the worker writes the frame meanwhile
// (a torn line is possible, it is a diagnostic).
void TraceDetector(int inputId, double base, double rms) {
    uintptr_t mgr = 0, di = 0, det = 0;
    if (!g_rs7_vg_dimgr || !ReadAt(g_rs7_vg_dimgr, &mgr) || !mgr) {
        return;
    }
    if (!ReadAt(mgr + (inputId == 0 ? 0x10 : 0x14), &di) || !di || !ReadAt(di + 4, &det) || !det) {
        return;
    }

    uint8_t f[2000];
    if (!SafeCopy(f, reinterpret_cast<const void*>(det + 0x5E8), sizeof f)) {
        return;
    }

    Rs7Log(
        "vguitar trace: base %.4f in-rms %.4f | frame t %.4f sclk %.4f serial %d onset %u | main %d | since %d %.2f | "
        "n %d c0 %d %.2f c1 %d %.2f c2 %d %.2f | lvl %.1f rel %.1f | fine %.2f smooth %.2f",
        base, rms, Get<double>(f, 0x720), Get<double>(f, 0x730), Get<int32_t>(f, 0x748), unsigned(f[0x7B5]),
        Get<int32_t>(f, 0x0C), Get<int32_t>(f, 0x14), double(Get<float>(f, 0x18)), Get<int32_t>(f, 0xBC),
        Get<int32_t>(f, 0x1C), double(Get<float>(f, 0x20)), Get<int32_t>(f, 0x24), double(Get<float>(f, 0x28)),
        Get<int32_t>(f, 0x2C), double(Get<float>(f, 0x30)), double(Get<float>(f, 0x700)), double(Get<float>(f, 0x750)),
        double(Get<float>(f, 0x7A8)), double(Get<float>(f, 0x7B0)));
}

DWORD WINAPI Watchdog(void*) {
    for (;;) {
        Sleep(250);
        const DWORD now = GetTickCount();
        if (now - DWORD(g_lastTickMs) < 1500) {
            continue;
        }

        EnterCriticalSection(&g_cs);
        if (g_sinceSummary || g_hasPending) {
            Summary("song stopped ticking");
        }
        LeaveCriticalSection(&g_cs);
    }
}
}  // namespace

void Rs7Vg_Configure(const wchar_t* iniPath, const wchar_t* csvPath) {
    InitializeCriticalSection(&g_cs);
    g_cfg = LoadConfig(iniPath);
    g_csvPath = csvPath;
    g_iniPath = iniPath;

    if (!g_cfg.on) {
        Rs7Log("config: [test] vguitar=0 -> virtual guitarist off (TICK/JUD1/JUD2/AUD1 resolved, not patched)");
        return;
    }

    g_csv = _wfsopen(csvPath, L"w", _SH_DENYWR);
    if (g_csv) {
        CsvWrite(CsvHeader());
    }
    Rs7Log(
        "config: [test] vguitar=1 -> VIRTUAL GUITARIST ON: mode %s%s%s, filter %ls%s, lead %.1f ms%s, gain %.2f%s, %s; "
        "CSV %ls%s",
        ModeName(g_cfg.mode), g_cfg.mode == Mode::Midi ? (" " + std::to_string(g_cfg.midi)).c_str() : "",
        g_cfg.modeValid ? "" : " (INVALID vguitar_mode, default chart)",
        g_cfg.filterAll ? L"all" : g_cfg.filterRaw.c_str(),
        g_cfg.filterValid ? "" : " (INVALID vguitar_filter, default all)", double(g_cfg.leadMs),
        g_cfg.leadValid ? "" : " (INVALID, 0)", double(g_cfg.gain), g_cfg.gainValid ? "" : " (INVALID, 0.3)",
        g_cfg.mix ? "mix" : "replace", csvPath, g_csv ? "" : " (could not open)");
    if (g_cfg.trace) {
        Rs7Log("config: [test] vguitar_trace=1 -> detector frame trace for 0.6 s after each onset (diagnostic)");
    }
    g_rs7_vg_on = 1;
    HANDLE h = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);

    if (h) {
        CloseHandle(h);
    }
}

bool Rs7Vg_Enabled() {
    return g_cfg.on;
}

void Rs7Vg_OnSongTuning(void* session, const int16_t* tuning, int count) {
    if (!g_cfg.on) {
        return;
    }

    g_tuneSession = session;
    g_tuningCount = count < 0 ? 0 : count > 8 ? 8 : count;

    for (int i = 0; i < 8; ++i) {
        g_tuning[i] = i < g_tuningCount ? tuning[i] : 0;
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnTick(void* session, float songTime) {
    __try {
        TickImpl(session, songTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in TICK - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnJudge(void* note, void* session) {
    __try {
        JudgeImpl(note, session);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in JUD1 - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnScore(int hit, void* session, void* note) {
    __try {
        ScoreImpl(hit, session, note);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in JUD2 - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnInput(void* device, void* akBuffer) {
    __try {
        InputImpl(device, akBuffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in AUD1 - virtual guitarist switched off");
    }
}
