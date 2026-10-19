// The per-guitar 7-string look's logic (string7.h): the SNG section-mask back-fill of string 6 (N13) and the
// visibility rule N5 applies every frame, the [neck] string7_v text. Values in, values out.
#include "features/string7/string7.h"
#include <cmath>
#include <cwchar>
#include <cwctype>

int Rs7S7_BackfillBit(uint8_t* sections, int count, uint32_t level, uint8_t bit, int* added) {
    int lit = 0;

    if (added) {
        *added = 0;
    }
    if (!sections || count <= 0 || level >= 32) {
        return 0;
    }

    for (int i = count - 1; i > 0; --i) {
        uint8_t& cur = sections[i * 0x58 + 0x34 + level];
        uint8_t& prev = sections[(i - 1) * 0x58 + 0x34 + level];

        if ((cur & bit) && !(prev & bit)) {
            prev = uint8_t(prev | bit);
            if (added) {
                *added += 1;
            }
            --i;
        }
    }

    for (int i = 0; i < count; ++i) {
        lit += (sections[i * 0x58 + 0x34 + level] & bit) ? 1 : 0;
    }

    return lit;
}

uint32_t rs7s7::g_fadeHoldMs = 1500;

rs7s7::Visibility rs7s7::Rs7S7_Visibility(bool seven, bool& armed, bool song, bool songEnded, bool fadedOut,
                                          bool fadeOutDone, bool unseen) {
    Disarm why = Disarm::None;

    if (armed && !song) {
        if ((fadedOut && fadeOutDone) || (songEnded && fadedOut)) {
            why = Disarm::FadedOut;
        } else if (unseen) {
            why = Disarm::Unseen;
        }
    }

    if (why != Disarm::None) {
        armed = false;
    }

    return Visibility{seven && (armed || song), why};
}

bool Rs7_ParseAtlasRow(const wchar_t* text, float* out) {
    if (!text) {
        return false;
    }

    while (std::iswspace(*text)) {
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

    while (std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < 0.0 || v > 0.9 + 1e-6) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}
