#pragma once
#include <cstdint>
#include <string>

// Extended tuning names for 7/8-string guitar and 5/6-string bass (GitHub issue #15, docs/ARCHITECTURE.md): the name
// logic (tuningname.cpp, values in and out, tests/tuner). The game side (the TJ / TU / TUT / TN entry points) is
// hooks_names.cpp, declared in hooks.h.
//
// The game names a tuning by matching string0..5 of the arrangement's manifest Tuning against the
// six-offset TuningDefinitions of static.psarc (three matchers: 0x56C950 / 0x56C300 manifest,
// 0x5B7750 tuner). A 7-string arrangement (manifest key RsxrString6) has
// six-string offsets that mean something else: B Standard is all zero and was shown as
// "E STANDARD". For such arrangements these hooks answer with the 7-string name:
//
//  - TJ (0x56C950 entry, Tuning JSON in hand): song rows (0x573B90 / 0x5750C0: display +138h,
//    raw +150h), arrangement metadata 0x69DCE0, 0x56C890. Reads RsxrString6 and string0..5 from
//    the JSON before the original runs (it releases the JSON), lets the original run, then assigns
//    both outputs.
//  - TU / TUT (0x56C300 entry / 0x56C408 inside it, where EDI = the arrangement's Tuning and
//    [ebp-88h] its Attributes): the eight UUID-based UI callers. Same, output = localised text.
//  - TN (0x5B7750 entry, int[6] in hand): the tuner (SP 0x73F75A, MP 0x743D85 / 0x743F26, pre-song
//    dialog 0x5B76F5, notification 0x5B10F0). 7-string when TK read RsxrString6 for exactly that
//    array (hooks.cpp keeps it per array) or the array is a 7-string tuner session's +50h.
//
// Names (Rocksmith's own spelling): offsets t[0..5] all k and t6 == k: "<B+k> Standard"; t6 == k - 2: drop of the
// 7th string ("Drop A" for k = 0, the "X Drop Y" form otherwise, as the game's "Eb Drop Db" / "D Drop C"); t6 3..5
// below k: a deeper drop ("Drop G", "A Drop F"); k = +1 (C) .. -9 (D); flats throughout (the game's Eb).
// Alternates by exact offsets: Extended Drop D / C / B, Double Drop D, Open G7. The name gets a 7-string mark, by
// default the suffix "7Str" ([tuner] name_suffix / name_prefix): "B Standard 7Str" ("8Str", "5Str", "6Str" on the
// other instruments, SuffixN).
// Anything else on a 7-string arrangement: the game's own name (or CUSTOM TUNING) with the mark.
// 6-string guitar and 4-string bass (no Rsxr key): untouched, the original runs alone.
//
// Display form: the game shows the localised UIName ("$[36578]Eb Standard" -> "E♭ STANDARD",
// maingame.csv, UTF-8): upper case and the Unicode accidentals U+266D / U+266F. English only for
// now (no $[id] tokens: 0x7BF420 copies text without "$" unchanged).
// CentOffset: the stock matchers append ": A<pitch>" (0x56C42C / 0x56CC2F, A = (int)(440 *
// 2^(cents/1200)) in float steps) to a matched name; 7-string names get the same.

namespace rsxrtn {

constexpr char kKey[] = "RsxrString6";

// t[0..5] = string0..5 (low E .. high e), t[6] = RsxrString6 (the extra low string, B1 base).
// The canonical name ("B Standard", "Bb Drop Ab", "Extended Drop D"), or nullptr: no 7-string name.
const char* Name7(const int t[7]);
// Offset bases: string 6 = B1, string 7 = F#1. Exact intervals, no octave folding.
const char* Name8(const int t[8]);
constexpr char kKey8[] = "RsxrString7";

// base with the 7-string mark: prefix + " " + base + " " + suffix (empty parts and their blank
// left out): ("B Standard", "", "7Str") -> "B Standard 7Str".
std::string Compose(const char* base, const char* prefix, const char* suffix);

// The game's display form: ASCII upper case; a word that is a note name with an accidental
// ("Bb", "C#") becomes letter + U+266D / U+266F in UTF-8 ("B♭", "C♯").
std::string Display(const std::string& name);

// The stock pitch reference of a non-zero CentOffset: (int)(440 * 2^(cents / 1200)), with the
// game's float roundings (0x56C44D..0x56C480). 440 for 0.
int ReferenceA(int cents);
// ": A<ReferenceA>" for cents != 0, "" for 0 (the game appends nothing then).
std::string Decoration(int cents);

// A stock display text (localised, maybe with the ": A<n>" decoration at the end) with the
// 7-string mark in display form around the name: ("E STANDARD: A436", "", "7Str") ->
// "E STANDARD 7STR: A436".
std::string MarkStock(const std::string& stock, const char* prefix, const char* suffix);

// [tuner] names / name_prefix / name_suffix (ASCII, at most 31 characters each).
struct Config {
    bool on = true;
    char prefix[32] = "";
    char suffix[32] = "7Str";
};

// The display texts for a 7-string arrangement: known names -> Display(Compose(name)) (+
// decoration, if `decorate`), unknown -> empty (the caller marks the stock text with MarkStock).
std::string KnownDisplay(const int t[7], const Config& c, int cents, bool decorate);
// The same for an 8-string arrangement (RsxrString6 + RsxrString7 present): Name8 + SuffixN(8).
std::string KnownDisplay8(const int t[8], const Config& c, int cents, bool decorate);

// Extended bass (features/bass): RsxrString4 (the low B, string 4) gives a 5-string bass, with RsxrString5 (the high C,
// string 5) a 6-string one; t[0..5] = string0..5 of the Tuning (the six stock offsets carry all of them). Every
// played string but the low B at the same offset k: the low B names it, like the 7-string guitar ("B Standard" for
// BEADG and BEADGC, "Drop A" with the low B two lower); anything else nullptr (the stock name, marked). 4-string bass:
// never.
constexpr char kKeyBass4[] = "RsxrString4";
constexpr char kKeyBass5[] = "RsxrString5";
const char* NameBass(const int t[6], int strings);
// The mark for `strings` strings from the 7-string one: the first '7' becomes the count ("7Str" -> "5Str",
// "7-String" -> "8-String"); a suffix without a '7' (or an empty one) gives "<n>Str".
std::string SuffixN(const char* suffix7, int strings);
// KnownDisplay for a 5/6-string bass: NameBass + SuffixN.
std::string KnownDisplayBass(const int t[6], int strings, const Config& c, int cents, bool decorate);

}  // namespace rsxrtn
