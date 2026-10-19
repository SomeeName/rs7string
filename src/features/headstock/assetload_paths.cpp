#include "features/headstock/assetload.h"
#include <cstring>
#include <cwchar>
#include <string>

// The pure name and path rules of the 7-string headstock model: which game resources are candidates, where the
// edited file lives, how [assets] values resolve.
using namespace rs7asset;

namespace {
// A game resource name that is a .nif path, split up: lower case with '/' separators, the last '/' and the file
// name without ".nif".
struct NifName {
    std::string low, base;
    size_t slash;
};

bool ParseNifName(const char* name, NifName& out) {
    if (!name) {
        return false;
    }

    const size_t len = std::strlen(name);
    if (len < 5 || len > 240 || _stricmp(name + len - 4, ".nif") != 0) {
        return false;
    }

    out.low.assign(name, len);

    for (char& ch : out.low) {
        ch = char((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch == '\\' ? '/' : ch);
    }

    out.slash = out.low.find_last_of('/');
    out.base = out.low.substr(out.slash + 1, out.low.size() - out.slash - 1 - 4);

    return !out.base.empty();
}

// "X:..." or a UNC path: the resolvers below keep it as it is.
bool IsAbsoluteWin(const std::wstring& v) {
    return (v.size() > 1 && v[1] == L':') || (v.size() > 1 && v[0] == L'\\' && v[1] == L'\\');
}

// `dir` ending in a path separator.
std::wstring WithSeparator(const std::wstring& dir) {
    std::wstring out = dir;

    if (out.back() != L'\\' && out.back() != L'/') {
        out += L'\\';
    }

    return out;
}

// An [assets] value without the blanks and tabs around it.
std::wstring Trimmed(std::wstring v) {
    while (!v.empty() && (v.back() == L' ' || v.back() == L'\t')) {
        v.pop_back();
    }

    size_t k = 0;

    while (k < v.size() && (v[k] == L' ' || v[k] == L'\t')) {
        ++k;
    }

    v.erase(0, k);

    return v;
}
}  // namespace

std::wstring Rs7Asset_EditedPath(const char* name, const std::wstring& dir, const std::wstring& suffix) {
    if (!name || dir.empty()) {
        return {};
    }

    NifName n;

    if (!ParseNifName(name, n) || n.low.find("/gameplay/guitar") == std::string::npos) {
        return {};
    }

    const std::string& base = n.base;

    for (char ch : base) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
            return {};
        }
    }

    if (base.find("..") != std::string::npos) {
        return {};
    }

    std::wstring out = WithSeparator(dir);

    out.append(base.begin(), base.end());
    out += suffix;
    out += L".nif";

    return out;
}

// The layouts the 7-string in-line model (headstock7_file / headstock7_file_lh) replaces. One row per
// value of Optionen > Spieleinstellungen > Gitarrenkopfplatte; the game loads exactly one frame per
// handedness from the row's folder. CONFIRMED (asset dump of the game's own packages + run logs):
//   "6 in Reihe": base\dependency_guitarstandardinlined{rh,lh}.psarc, guitar_standard_inline/
//                 epi_wilshire_cherry.nif / epi_whilshire_cherry_lh.nif, stock part Wilshire_RH / Wilshire_LH
//   "3+3":        base\dependency_guitarstandard3x3{,lh}.psarc, guitar_standard/ gib_lespaul.nif /
//                 gib_lespaul_lh.nif, stock part LP_RH / LP_LH; pegs base\dependency_guitarstandard3x3peg.psarc
//                 guitar_standard/peg_bakelite.nif (one model for both hands)
// Attach transform: none for any row. Both models hang under an identity "Scene Root" and carry their
// geometry in that space; the nut (strings start at x = 0, z = 53.6..53.7 in glTF axes) and the string
// spans (+-5.5) coincide between Wilshire_RH and LP_RH (and the LH twins, mirrored in y), so the
// in-line model sits on a 3+3 neck exactly where it sits on an in-line one. The stock parts are
// culled by Show() as "every stock NiNode/NiMesh sibling of rs7_alt", not by name; stockRh/stockLh
// only document what that is (and are logged with the layout).
// 3+3 pegs and letters (fix/headstock2): the first 3+3 attempt (tests/shots/hs33/t4_33_closeup.png) showed
// bakelite pegs and the letters G B e B turned round. The letters are the stock rule of
// UI_PlayerTuner_PlacePegLetters 0x5B1A10 (CONFIRMED 0x5B1B53..0x5B1B7D: in-line byte InstrumentInfo +0Ah = 0
// and not inverted -> the letters of pegs >= count/2 turned round; 7 pegs -> indices 3..6 = G B e B, exactly
// the shot): LT1 reports such a guitar as in-line. The pegs: the game picks the peg pool model by the profile
// option (Rnd_Guitar_LoadDependencies 0x5965A0), so peg_bakelite.nif gets the in-line peg
// (epi_whilshire_peg.nif + wilshire_hardware.dds from the in-line package, the game's own reader) merged in as
// rs7_alt, culled; Show() swaps it in per peg of a guitar that shows the 7-string headstock, and the bakelite
// locator_letter / Locator_Arrow of those pegs are renamed so the game's name lookups find the in-line ones.
const HeadLayout kLayouts[] = {
    {"6 in Reihe", "/gameplay/guitar_standard_inline/", "Wilshire_RH", "Wilshire_LH", nullptr},
    {"3+3", "/gameplay/guitar_standard/", "LP_RH", "LP_LH", "peg_bakelite"},
};

static_assert(sizeof kLayouts / sizeof kLayouts[0] == kLayoutCount, "kLayoutCount");

const HeadLayout* Rs7Asset_LayoutOf(const char* name, bool* leftHanded) {
    NifName n;

    if (!ParseNifName(name, n)) {
        return nullptr;
    }

    const bool lh = n.base.size() >= 3 && n.base.compare(n.base.size() - 3, 3, "_lh") == 0;

    for (const HeadLayout& l : kLayouts) {
        const size_t flen = std::strlen(l.folder);
        const size_t at = n.low.find(l.folder);
        if (at == std::string::npos || n.slash != at + flen - 1) {
            continue;  // directly in that folder
        }

        if (leftHanded) {
            *leftHanded = lh;
        }
        return &l;
    }

    return nullptr;
}

bool Rs7Asset_IsRhCandidate(const char* name) {
    bool lh = false;

    return Rs7Asset_LayoutOf(name, &lh) && !lh;
}

bool Rs7Asset_IsLayoutPeg(const char* name) {
    bool lh = false;
    const HeadLayout* l = Rs7Asset_LayoutOf(name, &lh);
    if (!l || !l->peg) {
        return false;
    }

    NifName n;

    return ParseNifName(name, n) && n.base == l->peg;
}

// The left-handed twin: the same folders, base name ending in "_lh" (epi_whilshire_cherry_lh.nif,
// gib_lespaul_lh.nif: the frames the game loads for a left-handed profile).
bool Rs7Asset_IsLhCandidate(const char* name) {
    bool lh = false;

    return Rs7Asset_LayoutOf(name, &lh) && lh;
}

std::wstring Rs7Asset_ResolveFile(const std::wstring& value, const std::wstring& dir) {
    const std::wstring v = Trimmed(value);

    if (v.empty() || _wcsicmp(v.c_str(), L"none") == 0 || _wcsicmp(v.c_str(), L"off") == 0 || v == L"0") {
        return {};
    }
    if (IsAbsoluteWin(v) || dir.empty()) {
        return v;
    }

    return WithSeparator(dir) + v;
}

std::wstring Rs7Asset_ResolveDir(const std::wstring& value, const std::wstring& dllDir) {
    std::wstring v = Trimmed(value);

    if (v.empty()) {
        v = kDefaultDir;
    }
    if (IsAbsoluteWin(v) || dllDir.empty()) {
        return v;
    }

    return WithSeparator(dllDir) + v;
}

std::string Rs7Asset_FolderOf(const char* name) {
    if (!name) {
        return {};
    }

    std::string s(name);
    const size_t k = s.find_last_of("/\\");

    return k == std::string::npos ? std::string() : s.substr(0, k + 1);
}
