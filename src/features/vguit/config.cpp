// [test] vguitar_* option parsing: text in, values out (tests/vguit). The ini reads are in hooks.cpp (LoadConfig).
#include "features/vguit/vguit.h"
#include "model/eight.h"
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace rs7vg {

const char* ModeName(Mode m) {
    switch (m) {
        case Mode::Off:
            return "off";
        case Mode::Chart:
            return "chart";
        case Mode::Game:
            return "game";
        case Mode::Midi:
            return "midi";
    }

    return "?";
}

namespace {
std::wstring Trim(const wchar_t* t) {
    while (*t && std::iswspace(*t)) {
        ++t;
    }

    std::wstring s(t);

    while (!s.empty() && std::iswspace(s.back())) {
        s.pop_back();
    }

    for (auto& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }

    return s;
}
}  // namespace

bool ParseMode(const wchar_t* text, Mode* mode, int* midi) {
    const std::wstring s = Trim(text);

    if (s == L"chart") {
        *mode = Mode::Chart;
        return true;
    }

    if (s == L"game") {
        *mode = Mode::Game;
        return true;
    }

    if (s == L"off") {
        *mode = Mode::Off;
        return true;
    }

    if (s.compare(0, 5, L"midi:") == 0 && s.size() > 5) {
        wchar_t* end = nullptr;
        const long v = std::wcstol(s.c_str() + 5, &end, 10);
        if (*end || v < 20 || v > 100) {
            return false;
        }

        *mode = Mode::Midi;
        *midi = static_cast<int>(v);
        return true;
    }

    return false;
}

bool ParseFilter(const wchar_t* text, uint8_t* mask, int* fret, bool* all) {
    const std::wstring s = Trim(text);

    if (s == L"all") {
        *mask = g_rs7_eight ? 0xFF : 0x7F;
        *fret = -1;
        *all = true;
        return true;
    }

    const size_t colon = s.find(L':');
    const std::wstring strings = s.substr(0, colon);
    if (strings.empty()) {
        return false;
    }

    uint8_t m = 0;

    for (wchar_t ch : strings) {
        if (ch < L'0' || ch > (g_rs7_eight ? L'7' : L'6')) {
            return false;
        }

        m = static_cast<uint8_t>(m | (1u << (ch - L'0')));
    }

    int f = -1;

    if (colon != std::wstring::npos) {
        wchar_t* end = nullptr;
        const std::wstring ft = s.substr(colon + 1);
        if (ft.empty()) {
            return false;
        }

        const long v = std::wcstol(ft.c_str(), &end, 10);
        if (*end || v < 0 || v > 30) {
            return false;
        }

        f = static_cast<int>(v);
    }

    *mask = m;
    *fret = f;
    *all = false;

    return true;
}

bool ParseFloat(const wchar_t* text, float lo, float hi, float* out) {
    while (*text && std::iswspace(*text)) {
        ++text;
    }

    if (!*text) {
        return false;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text) {
        return false;
    }

    while (*end && std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < lo || v > hi) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}

const char* Chord6Name(Chord6 m) {
    switch (m) {
        case Chord6::Play:
            return "play";
        case Chord6::Omit:
            return "omit";
        case Chord6::Only:
            return "only";
        case Chord6::Shift:
            return "shift";
        case Chord6::OmitLow:
            return "omitlow";
        case Chord6::OmitHigh:
            return "omithigh";
    }

    return "?";
}

bool ParseChord6(const wchar_t* text, Chord6* mode, int* shift) {
    const std::wstring s = Trim(text);

    if (s == L"play") {
        *mode = Chord6::Play;
        *shift = 0;
        return true;
    }

    if (s == L"omit") {
        *mode = Chord6::Omit;
        *shift = 0;
        return true;
    }

    if (s == L"only") {
        *mode = Chord6::Only;
        *shift = 0;
        return true;
    }

    if (s == L"omitlow") {
        *mode = Chord6::OmitLow;
        *shift = 0;
        return true;
    }

    if (s == L"omithigh") {
        *mode = Chord6::OmitHigh;
        *shift = 0;
        return true;
    }

    if (s.size() >= 2 && (s[0] == L'+' || s[0] == L'-')) {
        wchar_t* end = nullptr;
        const long v = std::wcstol(s.c_str(), &end, 10);
        if (*end || v == 0 || v < -24 || v > 24) {
            return false;
        }

        *mode = Chord6::Shift;
        *shift = static_cast<int>(v);
        return true;
    }

    return false;
}

}  // namespace rs7vg
