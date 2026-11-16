#include "features/tuner/tuningname.h"
#include "features/tuner/tuner.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Seven- and eight-string tuning names (tuningname.h): the name tables and the display forms. Values in and out
// (tests/tuner); the game side is hooks_names.cpp.

// ---- the name table (pure) ----
namespace {
struct Family {
    int k;             // offset of strings 0..5 (and of the 7th string for Standard)
    const char* std;   // "<B+k> Standard"
    const char* drop;  // the 7th string two semitones lower
};

// k = +1 (C) .. -9 (D) (issue #15); flats as the game's "Eb Standard" /
// "Eb Drop Db" (no C#/Db in this range except the Db of Eb Drop Db).
constexpr Family kFamilies[] = {
    {+1, "C Standard", "C Drop Bb"},   {0, "B Standard", "Drop A"},       {-1, "Bb Standard", "Bb Drop Ab"},
    {-2, "A Standard", "A Drop G"},    {-3, "Ab Standard", "Ab Drop Gb"}, {-4, "G Standard", "G Drop F"},
    {-5, "Gb Standard", "Gb Drop E"},  {-6, "F Standard", "F Drop Eb"},   {-7, "E Standard", "E Drop D"},
    {-8, "Eb Standard", "Eb Drop Db"}, {-9, "D Standard", "D Drop C"},
};

struct Alternate {
    int t[7];  // string0..5, Rs7String6
    const char* name;
};

// Low-to-high notes of the issue -> offsets from B1 E2 A2 D3 G3 B3 E4 (the 7th string first in
// the issue's lists, last here). Extended Drop D/C/B: a six-string Drop D / D Drop C / C# Drop B
// with the 7th string a fourth below string 0. Double Drop D and Open G7: the issue gives pitch
// classes only; these registers (D2 A2 D3 G3 B3 D4 D4, G1 D2 G2 B2 D3 F3 G3) are a HYPOTHESIS
// (docs/ARCHITECTURE.md) until a chart or the owner confirms them.
constexpr Alternate kAlternates[] = {
    {{-2, 0, 0, 0, 0, 0, -2}, "Extended Drop D"},      {{-4, -2, -2, -2, -2, -2, -4}, "Extended Drop C"},
    {{-5, -3, -3, -3, -3, -3, -5}, "Extended Drop B"}, {{5, 5, 5, 4, 3, -2, 3}, "Double Drop D"},
    {{-2, -2, -3, -5, -6, -9, -4}, "Open G7"},
};

// The 8-string families (string 7 = F#1). Rocksmith spelling: F# for the canonical instrument, flats otherwise.
constexpr Family kFamilies8[] = {
    {1, "G Standard", "G Drop F"},     {0, "F# Standard", "Drop E"},      {-1, "F Standard", "F Drop Eb"},
    {-2, "E Standard", "E Drop D"},    {-3, "Eb Standard", "Eb Drop Db"}, {-4, "D Standard", "D Drop C"},
    {-5, "Db Standard", "Db Drop B"},  {-6, "C Standard", "C Drop Bb"},   {-7, "B Standard", "B Drop A"},
    {-8, "Bb Standard", "Bb Drop Ab"}, {-9, "A Standard", "A Drop G"}};
constexpr size_t kFamilyCount = sizeof kFamilies / sizeof kFamilies[0];
static_assert(kFamilyCount == sizeof kFamilies8 / sizeof kFamilies8[0], "one deep-drop table row per family");

// Deeper drops: the lowest string 3..5 semitones below its family. "Drop <note>" in the family the instrument is
// named after (k = 0: 7-string "Drop G", 8-string "Drop D"), "<family note> Drop <note>" otherwise ("A Drop F"). Low
// notes in the families' spelling: flats, F# kept.
constexpr int kDeepMin = 3, kDeepMax = 5;
constexpr const char* kNotes[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

struct DeepNames {
    std::string name[2][kFamilyCount][kDeepMax - kDeepMin + 1];  // [0] 7-string / bass (low B), [1] 8-string (low F#)
};

// Built once (thread-safe static init): the hooks ask from the game's threads.
const DeepNames& Deep() {
    static const DeepNames names = [] {
        DeepNames n;
        const Family* tables[2] = {kFamilies, kFamilies8};
        const int lowPc[2] = {11, 6};  // B, F#

        for (int b = 0; b < 2; ++b) {
            for (size_t i = 0; i < kFamilyCount; ++i) {
                const Family& f = tables[b][i];
                const std::string note(f.std, std::strchr(f.std, ' '));

                for (int d = kDeepMin; d <= kDeepMax; ++d) {
                    const std::string low = kNotes[((lowPc[b] + f.k - d) % 12 + 12) % 12];

                    n.name[b][i][d - kDeepMin] = f.k == 0 ? "Drop " + low : note + " Drop " + low;
                }
            }
        }

        return n;
    }();

    return names;
}

// Strings 0.. at offset k and the lowest string at k - d: Standard (d = 0), the table's drop (d = 2), a deep drop
// (3..5); anything else nullptr. `table` 0 = 7-string families, 1 = 8-string families.
const char* FamilyName(int table, int k, int d) {
    const Family* families = table ? kFamilies8 : kFamilies;

    for (size_t i = 0; i < kFamilyCount; ++i) {
        const Family& f = families[i];

        if (f.k != k) {
            continue;
        }
        if (d == 0) {
            return f.std;
        }
        if (d == 2) {
            return f.drop;
        }
        if (d >= kDeepMin && d <= kDeepMax) {
            return Deep().name[table][i][d - kDeepMin].c_str();
        }

        return nullptr;
    }

    return nullptr;
}

bool IsNoteWithAccidental(const std::string& w) {
    return w.size() == 2 && w[0] >= 'A' && w[0] <= 'G' && (w[1] == 'b' || w[1] == '#');
}

// A known name's display text (+ the decoration, if `decorate`); no name: empty.
std::string Known(const char* name, const rs7tn::Config& c, const char* suffix, int cents, bool decorate) {
    if (!name) {
        return {};
    }

    std::string out = rs7tn::Display(rs7tn::Compose(name, c.prefix, suffix));

    if (decorate) {
        out += rs7tn::Decoration(cents);
    }

    return out;
}
}  // namespace

const char* rs7tn::Name7(const int t[7]) {
    if (!t) {
        return nullptr;
    }

    for (const Alternate& a : kAlternates) {
        if (std::memcmp(a.t, t, sizeof a.t) == 0) {
            return a.name;
        }
    }

    for (int s = 1; s < 6; ++s) {
        if (t[s] != t[0]) {
            return nullptr;
        }
    }

    return FamilyName(0, t[0], t[0] - t[6]);
}

const char* rs7tn::Name8(const int t[8]) {
    if (!t) {
        return nullptr;
    }

    for (int s = 1; s < 7; ++s) {
        if (t[s] != t[0]) {
            return nullptr;
        }
    }

    return FamilyName(1, t[0], t[0] - t[7]);
}

std::string rs7tn::KnownDisplay8(const int t[8], const Config& c, int cents, bool decorate) {
    return Known(c.on ? Name8(t) : nullptr, c, SuffixN(c.suffix, 8).c_str(), cents, decorate);
}

const char* rs7tn::NameBass(const int t[6], int strings) {
    if (strings < 5 || strings > 6) {
        return nullptr;
    }

    // Every played string but the low B (string 4) at the same offset k; the low B names the family like the 7-string
    // guitar's 7th string: at k "B Standard", at k - 2 "Drop A", 3..5 lower "Drop G".
    const int k = t[0];

    for (int s = 1; s < strings; ++s) {
        if (s != 4 && t[s] != k) {
            return nullptr;
        }
    }

    const int seven[7] = {k, k, k, k, k, k, t[4]};

    return Name7(seven);
}

std::string rs7tn::SuffixN(const char* suffix7, int strings) {
    std::string s = suffix7 ? suffix7 : "";
    const size_t i = s.find('7');
    const char digit = char('0' + strings);

    if (i == std::string::npos) {
        return std::string(1, digit) + "Str";
    }

    s[i] = digit;

    return s;
}

std::string rs7tn::KnownDisplayBass(const int t[6], int strings, const Config& c, int cents, bool decorate) {
    return Known(c.on ? NameBass(t, strings) : nullptr, c, SuffixN(c.suffix, strings).c_str(), cents, decorate);
}

std::string rs7tn::Compose(const char* base, const char* prefix, const char* suffix) {
    std::string out;

    if (prefix && *prefix) {
        out = std::string(prefix) + " ";
    }
    out += base ? base : "";
    if (suffix && *suffix) {
        out += std::string(" ") + suffix;
    }

    return out;
}

std::string rs7tn::Display(const std::string& name) {
    std::string out;
    size_t i = 0;

    while (i < name.size()) {
        if (name[i] == ' ') {
            out += ' ';
            ++i;
            continue;
        }

        const size_t j = name.find(' ', i);
        const std::string w = name.substr(i, j == std::string::npos ? std::string::npos : j - i);

        if (IsNoteWithAccidental(w)) {
            out += w[0];
            out += w[1] == 'b' ? "\xE2\x99\xAD" : "\xE2\x99\xAF";  // U+266D flat, U+266F sharp
        } else {
            for (char c : w) {
                out += (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
            }
        }

        i += w.size();
    }

    return out;
}

int rs7tn::ReferenceA(int cents) {
    if (!cents) {
        return 440;
    }

    const float x = static_cast<float>(static_cast<double>(cents) / 1200.0);  // FILD, FDIV, FSTP float
    const float p = std::pow(2.0f, x);                                        // 0x4E4AF0: float pow
    const float a = static_cast<float>(static_cast<double>(p) * 440.0);       // FMUL double, FSTP float

    return static_cast<int>(a);  // 0xF5B0D0: truncation
}

std::string rs7tn::Decoration(int cents) {
    if (!cents) {
        return {};
    }

    char b[24];

    std::snprintf(b, sizeof b, ": A%d", ReferenceA(cents));

    return b;
}

std::string rs7tn::MarkStock(const std::string& stock, const char* prefix, const char* suffix) {
    // The stock decoration ": A<digits>" at the end stays at the end.
    std::string name = stock, tail;
    const size_t p = stock.rfind(": A");

    if (p != std::string::npos && p + 3 < stock.size()) {
        bool digits = true;

        for (size_t i = p + 3; i < stock.size(); ++i) {
            digits = digits && stock[i] >= '0' && stock[i] <= '9';
        }

        if (digits) {
            name = stock.substr(0, p);
            tail = stock.substr(p);
        }
    }

    std::string out;

    if (prefix && *prefix) {
        out = Display(prefix) + " ";
    }
    out += name;
    if (suffix && *suffix) {
        out += " " + Display(suffix);
    }

    return out + tail;
}

std::string rs7tn::KnownDisplay(const int t[7], const Config& c, int cents, bool decorate) {
    return Known(c.on ? Name7(t) : nullptr, c, c.suffix, cents, decorate);
}

bool Rs7_ValidNameAffix(const std::wstring& text) {
    if (text.size() > 31) {
        return false;
    }

    for (wchar_t ch : text) {
        if (ch < 0x20 || ch > 0x7E) {
            return false;
        }
    }

    return true;
}
