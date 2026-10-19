#pragma once
#include "features/vguit/vguit.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

// The virtual guitarist's glue (hooks*.cpp): its state, the game layouts it reads and the functions the glue files
// share. Not for other code (use vguit.h).

extern "C" uintptr_t g_rs7_frame_global;  // X_DT: &0x135FBFC (song clock at [it]+0D8h)

// The SEH-guarded C entries of the site hooks (hooks.cpp; called by hooks_mid.cpp, exported for the emulator tests).
extern "C" {
__declspec(dllexport) void __cdecl Rs7Vg_OnTick(void* session, float songTime);
__declspec(dllexport) void __cdecl Rs7Vg_OnJudge(void* note, void* session);
__declspec(dllexport) void __cdecl Rs7Vg_OnScore(int hit, void* session, void* note);
__declspec(dllexport) void __cdecl Rs7Vg_OnInput(void* device, void* akBuffer);
}

namespace rs7vg {

// Layouts (virtual-guitarist.md sections 6/7, data.md).
constexpr uint32_t kSessInput = 0x1C, kSessNotesBegin = 0x41C, kSessNotesEnd = 0x420;
constexpr uint32_t kObjSng = 0x2C, kObjTemplate = 0x30, kObjTime = 0x38, kObjFlag = 0x50, kObjSingle = 0x51,
                   kObjChord = 0x52, kObjMidi = 0x58, kObjFwd = 0x94, kObjBack = 0x98, kObjDetected = 0xC1,
                   kObjFinished = 0xC3, kObjHitTime = 0xC4, kObjAttempts = 0x174, kObjSize = 0x178;
constexpr uint32_t kSngMask = 0x00, kSngString = 0x10, kSngFret = 0x11, kSngSlideTo = 0x34, kSngSlideUnp = 0x35,
                   kSngVibrato = 0x3A, kSngSustain = 0x3C, kSngMaxBend = 0x40, kSngBend = 0x44, kSngBendCount = 0x1C4,
                   kSngSize = 0x1C8;
constexpr uint32_t kTplFrets = 0x04, kTplNotes = 0x10, kTplSize = 0x48;

template <class T>
T Get(const uint8_t* b, uint32_t off) {
    T v;

    std::memcpy(&v, b + off, sizeof v);

    return v;
}

template <class T>
bool ReadAt(uintptr_t addr, T* out) {
    return Rs7_SafeCopy(out, reinterpret_cast<const void*>(addr), sizeof(T));
}

constexpr int kEvents = 256;
constexpr int kMaxNotes = 2048;   // live note objects read per TICK
constexpr int kMaxFrames = 8192;  // samples per input buffer

// Everything the feature keeps between calls. Threads: the game thread (TICK / JUD1 / JUD2), the audio thread (AUD1)
// and the watchdog; who may touch what is noted per group.
struct State {
    Config cfg;
    std::wstring csvPath, iniPath;
    FILE* csv = nullptr;
    bool csvWarned = false;
    CRITICAL_SECTION cs;               // report state (game thread + watchdog)
    SRWLOCK schedLock = SRWLOCK_INIT;  // schedule (game thread + audio thread)

    // Tuning per session (S4).
    void* tuneSession = nullptr;
    Tuning tuning;

    // Session tracking (game thread).
    void* session = nullptr;
    volatile LONG sessionInput = -1;  // session+1Ch, read by the audio thread
    volatile LONG lastTickMs = 0;
    DWORD ownTickMs = 0;
    volatile LONG active = 0;  // a session ticked within the last second (audio thread reads)
    float lastTime = 0;
    int song = 0;
    int idx = 0;
    bool aheadLogged = false;
    int lateNotes = 0;
    bool scheduleFullLogged = false;
    const void* notePtrs[kMaxNotes];

    // Report state (cs).
    Record pending;
    const void* pendingObj = nullptr;
    bool hasPending = false;
    Totals totals;
    Counters counters;
    int sinceSummary = 0;

    // Schedule (schedLock).
    Event ev[kEvents];
    volatile LONG flush = 0;  // game thread asks the audio thread to drop its voices (time jump / new song)

    // Audio thread only.
    Player player;
    double lastBase = -1;
    unsigned buffers = 0;
    bool formatLogged = false;
    bool badFormat = false;
    DWORD lastStatMs = 0;
    int clockFallbacks = 0;
    float mixBuf[kMaxFrames];
    double traceUntil = -1;
};

extern State g_vg;

// hooks_session.cpp (game thread)
void TickImpl(void* session, float time);
bool ReadColumns(const void* tpl, Columns* c);
// The master song clock ([X_DT]+0D8h); false while the frame global is unreadable.
bool MasterClock(float* clock);

// hooks_report.cpp (game thread; Summary also the watchdog, with cs held)
void JudgeImpl(void* note, void* session);
void ScoreImpl(int hit, void* session, void* note);
void CsvWrite(const std::string& s);
void ReadCounters(void* session, Counters* c);
void Summary(const char* why);
DWORD WINAPI Watchdog(void*);

// hooks_audio.cpp (audio thread)
void InputImpl(void* dev, void* akbuf);

}  // namespace rs7vg
