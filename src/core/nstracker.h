#pragma once
#include <cstdint>

// Native N-slot chord tracking (issue #18 stage 2): the technique tracker's chord evals over strings
// 0-7. A tracked chord (note mask 2000h) gets a parent eval and, when its strings carry different
// techniques, one child eval per sounding string (Play_Tracker_AddNoteOrChord 0x874AB0). Children of
// strings 0-5 live in the parent's subEval[6] (+0C04h); the eval cannot grow (100 inline slots of
// 0xC28 bytes), so the children of strings 6/7 are bytes at +0C1Eh/+0C1Fh (core/nslot.h kEvChild67).
//
// Rs7Ns_Aggregate is the C++ form of the three unrolled child queries 0x875780 / 0x875A10 / 0x875C90
// as one loop over the eight strings (same result for strings 0-5, bit for bit):
//   whole chord (eval +0C1Dh): the parent's own pair of flags;
//   per string: AND over the children of their flag pair (field, field + 20h), a child with neither
//   +64h nor +84h set either fails the chord (0x875780) or is skipped (0x875A10 / 0x875C90);
//   string query (stringPtr != nullptr, 0x875A10 / 0x875C90 only): that string's child alone.
namespace rs7track {
constexpr uint32_t kEvalSize = 0xC28;
constexpr uint32_t kEvIsChord = 0xC1C, kEvWhole = 0xC1D, kEvSub = 0xC04;
// Child eval slot of string s of a parent eval (-1 = none).
int Child(const uint8_t* eval, int s);
uint8_t Aggregate(const uint8_t* eval, const uint8_t* tracker, int field, bool zeroOnEmpty, const uint8_t* stringPtr);
}  // namespace rs7track

// AG1-AG3 (asm stubs): ESI = parent eval, EDI = tracker (+8 = evals[0]), EDX = string pointer or 0.
extern "C" __declspec(dllexport) uint8_t __cdecl Rs7Ns_Aggregate(const uint8_t* eval, const uint8_t* tracker, int field,
                                                                 int zeroOnEmpty, const uint8_t* stringPtr);
