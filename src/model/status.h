#pragma once
#include <cstdint>

// Read-only status for the overlay's status panel: what Rsxr_Startup did and what the last song
// setup (S4) saw. Written by the startup worker / the game thread, copied out by the render thread
// (an SRW lock around plain structs; no allocation).

enum class RsxrStartState : uint8_t { Starting, Patched, Failed };

struct RsxrStartStatus {
    RsxrStartState state = RsxrStartState::Starting;
    unsigned written = 0, resolved = 0;  // sites written / sites resolved (all or nothing)
    char protect[24] = {};               // memory protection method
    char error[160] = {};                // Failed: why
};

void RsxrStatus_SetStart(const RsxrStartStatus& s);
RsxrStartStatus RsxrStatus_Start();

struct RsxrSongStatus {
    long songs = 0;   // song setups seen (0 = none yet)
    int entries = 0;  // SNG tuning entries (6 to 8 for a guitar, 4 or 6 for a bass)
    int strings = 0;  // strings the song plays (guitar 6 to 8, bass 4 to 6)
    bool bass = false;
    bool active = false;  // 7-string mode on
    int t6 = 0;           // the 7th string's offset (low B = MIDI rsxr::kLowB + t6), 0 when not active
    int16_t tuning[8] = {};
};

void RsxrStatus_SetSong(int entries, int strings, bool bass, bool active, const int16_t* tuning);
RsxrSongStatus RsxrStatus_Song();

// The overlay's "song" rows for `s` (label, text; `on` = highlighted). Returns the row count (at most `max`).
struct RsxrStatusRow {
    const char* label;
    char text[96];
    bool on;
};

int RsxrStatus_SongRows(const RsxrSongStatus& s, RsxrStatusRow* rows, int max);
