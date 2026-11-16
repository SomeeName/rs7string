#pragma once
#include "model/slots.h"
#include "model/tuning.h"
#include <cstdint>

// Extended bass (docs/architecture/06-detection-and-scoring.md "Extended bass"): single notes on a fifth (low B,
// string 4, B0 = MIDI 23) or sixth (high C, string 5, C3 = MIDI 48) bass string, on their own lane and with the right
// expected pitch. Stock bass strings stay 0-3. Natively like the 7-string guitar: C-lite builds the bass with 5 / 6
// strings (GKB, Rs7String4 / Rs7String5 manifest keys), its attach points 4 / 5 continue the stock row
// (features/strings Rs7Gs_AttachExt), the game lays out lanes, strings, frets and pegs itself (edited headstock
// model with Locator_Peg_5 / 6). This unit adds what the game cannot know: the expected pitch (pitch feature: PE0,
// PC1-PC4) and the detector's open MIDI and lowest open MIDI (pitch: PT1, PR0, S4; the raw floor follows it,
// rs7p::Floor).
//
// Activation: only an arrangement whose SNG carries an RS7X block with STRS instrument 1 (docs/release/RS7X-SPEC.md)
// and is played on a bass detector. Everything else (4-string bass, guitar) keeps the stock behaviour.
namespace rs7bass {
constexpr uint32_t kStockStrings = rs7::kStockBassStrings;  // the native count of a stock bass arrangement
constexpr int kBaseB = rs7::kBassOpenMidi[4];               // string 4: B0
constexpr int kBaseC = rs7::kBassOpenMidi[5];               // string 5: C3
constexpr int kNoBase = -1;

// The song's bass string count: 5 or 6 for an RS7X bass block on a bass detector (instrument 2), else 0 (stock).
uint32_t SongStrings(bool hasBlock, int blockInstrument, int blockStrings, int detInstrument);

// Base MIDI of extra string `string` (4 -> 23, 5 -> 48) when `strings` (0, 5 or 6) includes it, else kNoBase.
// Inline and integer only: the light-mode pitch callbacks (PE0, PC1, PC4) call it.
inline int ExtraBase(uint32_t string, uint32_t strings) {
    if (strings > 6 || string < kStockStrings || string >= strings) {
        return kNoBase;
    }

    return string == 4 ? kBaseB : kBaseC;
}

// Local co-op: song setup (S4) runs once per player, on that player's detector. Each setup is kept per detector, so a
// guitar player's setup no longer clears the bass player's mode. A setup more than kRoundMs older than a new one
// belongs to an earlier song and is dropped (HYPOTHESIS: the players' setups of one song run back to back; the
// "song setup" log lines show it).
constexpr int kPlayers = 2;
constexpr uint32_t kRoundMs = 5000;

struct Setup {
    const void* det;   // the player's detector
    uint32_t strings;  // 5 / 6: an extended bass on it, else 0
    int instrument;    // the detector's instrument (2 = bass)
    uint32_t ms;       // GetTickCount of the setup
};

struct Players {
    Setup p[kPlayers];
};

// Records `det`'s setup at `ms` (an earlier song's setups go first).
void SetupPlayer(Players& t, const void* det, int instrument, uint32_t strings, uint32_t ms);
// The extended-bass string count set up on `det` (0: none).
uint32_t StringsOf(const Players& t, const void* det);
// The largest extended-bass string count of this song's players (0: none).
uint32_t MaxStrings(const Players& t);
// Whether a player of this song plays a guitar (instrument other than 2).
bool AnyGuitar(const Players& t);

// The lowest open MIDI of an active song: the stock value lowered to base + tuning of every extra string.
int Lowest(int stock, const int16_t tuning[6], uint32_t strings);

// The low B's main-string bend follows the detected pitch (Rnd_Note_UpdateInProgress 0x7E3700 reads it through
// 0x4EA090; semitones = tracked pitch - expected MIDI, a failed read or a negative value releases the bend at full
// speed). At B0..F#1 (31-46 Hz) the tracked pitch drops out (-1) or falls back to the unbent note for single frames, so
// the string snapped back and the bend never showed (in-game poll: E1 bent toward F#1 read 29.6, -1, 28.3, 29.5, 28.1
// ...). With a real bass the tracker also loses the decaying low B for long stretches (the bend held for about a
// second, then snapped back with the old 150 ms limit). BendHold keeps the last good reading of the note over every
// frame without a reading for as long as the note lasts; a reading more than kBendDrop below it counts only once it has
// stayed down for kBendReleaseSec (a real release), single frames back at the unbent pitch are held. Rises are taken at
// once.
constexpr float kBendReleaseSec = 0.15f;  // how long a lower reading must last to release (song seconds)
constexpr float kBendDrop = 0.75f;        // a fall larger than this is a dropout until it lasts (semitones)

struct BendHold {
    const void* note = nullptr;  // the note the reading belongs to (a new note starts over)
    float semis = 0.0f;          // the held reading
    float time = 0.0f;           // the song time of the last read
    float lowSince = -1.0f;      // since when the readings are below the held one (-1: they are not)
    bool valid = false;
    // Per-note counts for the log (Rs7_Mid_PBH): reads, reads without a pitch, low reads held, longest gap.
    uint32_t reads = 0, missed = 0, low = 0;
    float gapSince = -1.0f, longestGap = 0.0f, peak = 0.0f;
};

// One read of the bend: `ok` = the stock read succeeded, `semis` its value, `now` the song time. Returns whether the
// read counts as successful; `*semis` may be replaced by the held value.
bool HoldBend(BendHold& h, const void* note, bool ok, float* semis, float now);

}  // namespace rs7bass

extern "C" {
// 0 = off; 5 / 6 = the largest extended bass of this song's players (S4). Read by the per-note pitch callbacks (light /
// sse, no lock): they only ask about strings 4 / 5, which only an extended bass plays.
__declspec(dllexport) extern volatile uint32_t g_rs7_bass_strings;
// The players' setups (rs7bass::SetupPlayer); game thread writes, readers compare the detector.
__declspec(dllexport) extern rs7bass::Players g_rs7_bass_players;
}

// S4 (pitch hooks_mode.cpp): binds or clears the mode for the song whose SNG tuning vector `tuning` is (song + 110h,
// CONFIRMED at 0x7DF0CC) on detector `det`. Returns the song's bass string count (0 = stock). Never throws.
uint32_t Rs7Bass_OnSong(const void* det, const void* tuning, int detInstrument) noexcept;
// The bass string count set up on this detector (0: none, or another detector).
uint32_t Rs7Bass_DetectorStrings(const void* det) noexcept;
