#pragma once
#include <cstdint>

// Chords with the 7th string: what is left outside the native N-slot chord path (core/nslot.h,
// issue #18 stage 2). Detection, the tracker, gems, open-string bars, tails, finger numbers, next-note
// targets and section masks all run through the game's own chord code over the T8/C8 records now;
// this module only keeps
//  - the 7-string arrangement state (S4) and string 6's pitch for C++ users (virtual guitarist, N15),
//  - the chord frame's string-axis span (B1/B2): the frame model has a fixed six-string height and no
//    game data decides it (docs/ARCHITECTURE.md), so a frame of a chord that uses
//    string 6 is scaled to enclose the 7th lane,
//  - the codec of the old M4 format (name[31] of the template), still read for packages built before
//    the RS7X block (core/nslot.cpp builds T8 from it).
namespace rs7chord {

constexpr uint32_t kTplFrets = 0x04;    // int8 frets[6]
constexpr uint32_t kTplFingers = 0x0A;  // int8 fingers[6]
constexpr uint32_t kTplMidi = 0x10;     // int32 midi[6]
constexpr uint32_t kTplName = 0x28;     // char name[32]
constexpr uint32_t kTplExt = 0x47;      // name[31]: the old-format string-6 column
constexpr uint32_t kTplSize = 0x48;

struct Ext {
    int fret = -1;  // string 6 fret, -1 = no string-6 column
    int finger = -1;
};

// Old-format codec: byte = (finger6 + 1) << 5 | (fret6 + 1), 0 = none (fret 0..30, finger -1..6).
bool Decode(uint8_t b, Ext* out);
uint8_t Encode(int fret, int finger);  // 0 if fret is out of 0..30 (no column)
// Decodes a 0x48-byte template image: false unless name[0..30] holds a NUL and name[31] decodes.
bool FromTemplateBytes(const uint8_t* tpl, Ext* out);

}  // namespace rs7chord

// S4 (song setup): 7-string mode of the arrangement that is starting, and string 6's tuning offset.
void Rs7Chord_OnSong(bool active, int t6);
// Whether a 7-string arrangement is active and the template (a T8 record, every template pointer at
// play time) uses string 6: its T8 slot 6.
bool Rs7Chord_Get(const void* tpl, rs7chord::Ext* out);
// Expected MIDI of string 6 at `fret` for the active arrangement (35 + t6 + fret).
int Rs7Chord_Midi6(int fret);

// B1: a chord frame's geometry component was built for `chordObj` (template at +30h) on the highway `player`
// (guitar at +37Ch): every frame of a 7/8-string arrangement is marked with the highest string its chord uses (5 =
// lanes 0-5 only). B2: for a marked component, fills g_rs7_box_scale / g_rs7_box_trans (the stock span values plus the
// frame's string-axis scale and translation, derived from the guitar's lane positions: core/frets.h ChordFrame) and
// returns 1; 0 when the frame is exactly the stock one (stock ApplySpan).
// S4: the arrangement's string count (bass: 4). Read by B1 when [chords] native_chordloops=1.
void Rs7Chord_SetSongStrings(int n);
// native_chordloops B1: the highest added string (6 or 7) the T8 record at `tpl` uses in a 7/8-string
// arrangement, else -1 (then the stock-six rule applies: a string-6 column gives 6, none gives 5).
int Rs7Chord_FrameTop(const void* tpl);
// Parses [chords] frame_scale: 0.5-3, anything else (empty, non-numeric, <= 0, too large) gives 1.134. The key is no
// longer used (the frame follows the lane positions); the parser stays for old ini files and tests.
float Rs7Chord_ParseFrameScale(const wchar_t* text);
extern "C" __declspec(dllexport) void __cdecl Rs7Chord_OnBoxGeom(void* geom, const void* chordObj, const void* player);
extern "C" __declspec(dllexport) int __cdecl Rs7Chord_BoxScale(const void* geom);
extern "C" __declspec(dllexport) float g_rs7_box_scale[3];
extern "C" __declspec(dllexport) float g_rs7_box_trans[3];
// [chords] frame_axis (0 x, 1 y, 2 z; default 1): the frame model's string axis. [chords] frame_scale is read but
// ignored (logged). Read at startup.
void Rs7Chord_Configure(const wchar_t* iniPath);
