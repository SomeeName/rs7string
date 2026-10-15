#pragma once
#include <cstddef>
#include <cstdint>

// Native N-slot chord data (issue #18 stage 2). The SNG's chord templates (0x48 bytes, six string
// slots) and ChordNotes records (0x948 bytes, six string slots) are converted once per song into
// relocated "superset" records; the game's own template / ChordNotes stride sites (begin + id * 0x48
// / 0x948) are redirected to them (TP*/CP* sites), and the chord loops that matter are widened in
// place (loop bound 6 -> 7, slot offsets moved to the N-slot arrays below), so the stock code handles
// the 7th string like strings 0-5.
//
// T8 (template superset, stride 0x80):
//   +0x00..+0x47  the stock template, unchanged (u32 flags, int8 frets[6] +4, int8 fingers[6] +0xA,
//                 int32 midi[6] +0x10, char name[32] +0x28): every reader that is not widened still
//                 sees a valid six-column record
//   +0x48 int8 frets[8]   +0x50 int8 fingers[8]   +0x58 int32 midi[8]   (-1 = string not in the chord)
//   +0x78 u8 slots (6, or the arrangement's string count), +0x79 u8 used-string mask, +0x7A..+0x7F 0
// C8 (ChordNotes superset, stride 0x15A8):
//   +0x000..+0x947 the stock record (u32 mask[6], BendData32[6] +0x18 (0x184 each), int8 slideTo[6]
//                 +0x930, int8 slideUnpitchTo[6] +0x936, int16 vibrato[6] +0x93C)
//   +0x948 u32 mask[8]  +0x968 int8 slideTo[8]  +0x970 int8 slideUnpitchTo[8]  +0x978 int16 vibrato[8]
//   +0x988 BendData32 bends[8]
// Slots 0-5 of the N-slot arrays are copies of the stock columns; slots 6 and 7 (7th / 8th string)
// come from the RS7X block (core/extblock.h) or, for packages built before the block, from the old
// name[31] byte of the template (7-string arrangements only).
namespace rs7ns {

constexpr int kSlots = 8;   // record slots: strings 0..7
constexpr int kNative = 7;  // strings the widened gameplay loops cover (0..6)
                            // diagrams7 additionally populates both tails for 8-string diagrams
constexpr uint32_t kTplSize = 0x48;
constexpr uint32_t kT8Size = 0x80;
constexpr uint32_t kT8Frets = 0x48, kT8Fingers = 0x50, kT8Midi = 0x58, kT8Slots = 0x78, kT8Used = 0x79;
constexpr uint32_t kCnSize = 0x948;
constexpr uint32_t kBendSize = 0x184;  // BendData32: 32 x 12-byte values + u32 usedCount (+0x180)
constexpr uint32_t kC8Mask = 0x948, kC8SlideTo = 0x968, kC8SlideUnp = 0x970, kC8Vib = 0x978, kC8Bends = 0x988;
constexpr uint32_t kC8Size = kC8Bends + kSlots * kBendSize;  // 0x15A8
constexpr int kOpenMidi[kSlots] = {40, 45, 50, 55, 59, 64, 35, 30};

// One extended column (string >= 6) of a template, as authored.
struct Col {
    int fret = -1;  // -1 = not in the chord
    int finger = -1;
};

// One extended string's technique data inside a ChordNotes record.
struct Tech {
    uint32_t mask = 0;
    int8_t slideTo = -1;
    int8_t slideUnpitchTo = -1;
    int16_t vibrato = 0;
    const void* bends = nullptr;  // bendCount x 12-byte values (the stock BendData32 value layout)
    int bendCount = 0;
};

// Old M4 format: name[31] = ((finger + 1) << 5) | (fret + 1), read only when the name's NUL lies
// in name[0..30]. False: no string-6 column.
bool DecodeName31(const uint8_t* tpl, Col* out);

// Builds one T8 from a stock template. ext[0..1] = strings 6 and 7 (fret -1: none); tuning = the
// SNG MetaData tuning (int16 offsets, n entries); capo = SNG capo byte (PostProcessNotes 0x55B4A2
// turns every fret equal to it into 0: applied to the extended frets the same way).
void BuildT8(const uint8_t* tpl, const Col ext[2], const int16_t* tuning, int n, uint8_t capo, uint8_t* out);
// Builds one C8 from a stock record; tech[0..1] = strings 6 and 7 (nullptr: none).
void BuildC8(const uint8_t* cn, const Tech* tech6, const Tech* tech7, uint8_t* out);
// Fixup 0x55D490 synthesizes ChordNotes for repeated chords inside handshapes: per sounding string
// palm mute (note 0x40) -> 0x40, fret-hand mute (note 8) -> 0x20000, note 0x4000000 -> same, open
// (fret 0 or fret == capo != 0xFF) -> 4. The same rule for an extended string of such a record.
uint32_t SynthMask(uint32_t noteMask, int fret, uint8_t capo);

}  // namespace rs7ns

// Registry read by the redirect stubs (asm, lock-free): stock vector begin -> superset array.
// Published fields are written last (tplBegin / cnBegin); a free slot has song == 0.
struct Rs7NsEntry {
    uintptr_t tplBegin;  // +0  stock template vector begin (song +94h), 0 = none
    uint32_t tplCount;   // +4
    uintptr_t t8;        // +8
    uintptr_t cnBegin;   // +0C stock ChordNotes vector begin (song +0ACh) after PostProcess, 0 = none yet
    uint32_t cnCount;    // +10
    uintptr_t c8;        // +14
    uintptr_t song;      // +18 SNG_Song* (0 = free slot)
    uint32_t origCn;     // +1C ChordNotes count at load (before Fixup appended synthesized records)
};

constexpr int kRs7NsEntries = 64;  // the asm lookups (stubs_nslot.cpp) loop over all of them
extern "C" __declspec(dllexport) Rs7NsEntry g_rs7_ns[kRs7NsEntries];

// XB1 (loader thread, end of SNG_Song_Load, after Rs7Ext_OnSngLoaded): builds the song's T8 array.
void Rs7Ns_OnSngLoaded(void* song);
// PP1 (loader thread, after SNG_Song_PostProcess): re-syncs the T8 prefixes, builds the C8 array.
extern "C" __declspec(dllexport) void __cdecl Rs7Ns_OnPostProcessed(void* song);
// SF1 (song destructor 0x558680, before the stock body): drops the song's arrays.
extern "C" __declspec(dllexport) void __cdecl Rs7Ns_OnSongFree(void* song);
// C++ users (virtual guitarist, tests): the T8 record a template pointer points at (nullptr if the
// pointer is not inside a registered T8 array).
const uint8_t* Rs7Ns_AsT8(const void* tpl);
const uint8_t* Rs7Ns_AsC8(const void* cn);
// Logging of the last builds (overlay status line).
int Rs7Ns_LiveSongs();

// ---- the tracker's Play_NoteEval (0xC28 bytes, 100 inline slots: it cannot grow). Its template copy
// (+274h) keeps midi[6] at +284h; the copy's name (+29Ch..+2BBh) has no reader once copied, so the
// N-slot tails live there and +284h is a contiguous int32 midi[8] (issue #18 stage 2):
namespace rs7ns {
constexpr uint32_t kEvMidi8 = 0x284;     // int32 midi[8]: [0..5] the template copy, [6..7] the tails
constexpr uint32_t kEvMidi67 = 0x29C;    // int32 midi[6], midi[7]
constexpr uint32_t kEvMask67 = 0x2A4;    // u32 ChordNotes mask[6], mask[7]
constexpr uint32_t kEvFret67 = 0x2AC;    // int8 fret[6], fret[7]
constexpr uint32_t kEvFinger67 = 0x2AE;  // int8 finger[6], finger[7]
constexpr uint32_t kEvSlide67 = 0x2B0;   // int8 slideTo[6], slideTo[7]
constexpr uint32_t kEvUnp67 = 0x2B2;     // int8 slideUnpitchTo[6], [7]
constexpr uint32_t kEvVib67 = 0x2B4;     // int16 vibrato[6], vibrato[7]
constexpr uint32_t kEvChild67 = 0xC1E;   // u8 child eval slot of strings 6, 7 (0xFF = none)
// Writes the eval's N-slot tails from its T8 / C8 (nullptr: none): Play_Eval_Build's tail (BT1).
void WriteEvalTails(uint8_t* eval, const uint8_t* t8, const uint8_t* c8);
// The empty tails (Play_Eval_ClearState, CS1).
void ClearEvalTails(uint8_t* eval);
}  // namespace rs7ns

// BT1: Play_Eval_Build 0x874180 copied the template / ChordNotes (T8 / C8 or nullptr) into `eval`.
extern "C" __declspec(dllexport) void __cdecl Rs7Ns_OnEvalBuilt(uint8_t* eval, const uint8_t* tpl, const uint8_t* cn);
// AM1: Play_AnalyzeChordMistake 0x8755C0 tries the chord shifted by `shift` semitones: its six shifted
// targets (arr6) plus the shifted strings 6/7 of the template (T8) -> out8.
extern "C" __declspec(dllexport) void __cdecl Rs7Ns_Shift8(int32_t* out8, const int32_t* arr6, const uint8_t* tpl,
                                                           int shift);
