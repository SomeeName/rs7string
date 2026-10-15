#pragma once
#include <cstdint>

// Read-only status for the overlay's status panel: what Rs7_Startup did and what the last song
// setup (S4) saw. Written by the startup worker / the game thread, copied out by the render thread
// (an SRW lock around plain structs; no allocation).

enum class Rs7StartState : uint8_t { Starting, Patched, Failed };

struct Rs7StartStatus {
    Rs7StartState state = Rs7StartState::Starting;
    unsigned written = 0, resolved = 0;  // sites written / sites resolved (all or nothing)
    char protect[24] = {};               // memory protection method
    char error[160] = {};                // Failed: why
};

void Rs7Status_SetStart(const Rs7StartStatus& s);
Rs7StartStatus Rs7Status_Start();

struct Rs7SongStatus {
    long songs = 0;   // song setups seen (0 = none yet)
    int entries = 0;  // SNG tuning entries (6, or 7 for a 7-string arrangement)
    bool bass = false;
    bool active = false;  // 7-string mode on
    int t6 = 0;           // the 7th string's offset (low B = MIDI 35 + t6)
    int16_t tuning[8] = {};
};

void Rs7Status_SetSong(int entries, bool bass, bool active, const int16_t* tuning);
Rs7SongStatus Rs7Status_Song();
