// Extended bass logic (bass.h): values in, values out.
#include "features/bass/bass.h"

#include <algorithm>

#include "model/extblock.h"
#include "model/instrument.h"

namespace rs7bass {
uint32_t SongStrings(bool hasBlock, int blockInstrument, int blockStrings, int detInstrument) {
    if (!hasBlock || blockInstrument != rs7ext::kInstrumentBass || !rs7::IsBassKind(detInstrument)) {
        return 0;
    }
    if (blockStrings < rs7ext::kMinBassStrings || blockStrings > rs7ext::kMaxBassStrings) {
        return 0;
    }

    return uint32_t(blockStrings);
}

int Lowest(int stock, const int16_t tuning[6], uint32_t strings) {
    int low = stock;

    for (uint32_t s = kStockStrings; s < 6; ++s) {
        const int base = ExtraBase(s, strings);

        if (base != kNoBase && base + tuning[s] < low) {
            low = base + tuning[s];
        }
    }

    return low;
}

void SetupPlayer(Players& t, const void* det, int instrument, uint32_t strings, uint32_t ms) {
    Setup* slot = nullptr;

    for (Setup& s : t.p) {
        if (s.det && ms - s.ms > kRoundMs) {
            s = Setup{};  // an earlier song
        }
    }

    for (Setup& s : t.p) {
        if (s.det == det) {
            slot = &s;
        }
    }

    for (Setup& s : t.p) {
        if (!slot && !s.det) {
            slot = &s;
        }
    }

    if (!slot) {
        slot = &t.p[0];

        for (Setup& s : t.p) {
            if (s.ms < slot->ms) {
                slot = &s;
            }
        }
    }

    *slot = Setup{det, strings, instrument, ms};
}

uint32_t StringsOf(const Players& t, const void* det) {
    for (const Setup& s : t.p) {
        if (det && s.det == det) {
            return s.strings;
        }
    }

    return 0;
}

uint32_t MaxStrings(const Players& t) {
    uint32_t n = 0;

    for (const Setup& s : t.p) {
        n = s.det && s.strings > n ? s.strings : n;
    }

    return n;
}

bool AnyGuitar(const Players& t) {
    for (const Setup& s : t.p) {
        if (s.det && !rs7::IsBassKind(s.instrument)) {
            return true;
        }
    }

    return false;
}

bool HoldBend(BendHold& h, const void* note, bool ok, float* semis, float now) {
    if (h.note != note || (h.valid && now < h.time)) {  // a new note, or the clock went back (restart)
        h = BendHold{};
        h.note = note;
    }

    ++h.reads;
    h.time = now;

    if (!(ok && *semis >= 0.0f)) {  // no pitch this frame: keep the held bend
        h.lowSince = -1.0f;         // a missing reading cannot establish a sustained release
        if (!h.valid) {
            return ok;
        }

        ++h.missed;
        h.gapSince = h.gapSince < 0.0f ? now : h.gapSince;
        h.longestGap = std::max(h.longestGap, now - h.gapSince);
        *semis = h.semis;
        return true;
    }

    h.gapSince = -1.0f;

    if (h.valid && *semis < h.semis - kBendDrop) {  // well below the held bend: a dropout until it lasts
        h.lowSince = h.lowSince < 0.0f ? now : h.lowSince;

        if (now - h.lowSince < kBendReleaseSec) {
            ++h.low;
            *semis = h.semis;
            return true;
        }
    }

    h.semis = *semis;
    h.peak = std::max(h.peak, *semis);
    h.lowSince = -1.0f;
    h.valid = true;

    return true;
}
}  // namespace rs7bass
