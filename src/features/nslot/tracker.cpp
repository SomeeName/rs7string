#include "features/nslot/tracker.h"
#include "features/nslot/nslot.h"
#include <cstring>

namespace rs7track {

int Child(const uint8_t* ev, int s) {
    if (s < 6) {
        int32_t c;

        std::memcpy(&c, ev + kEvSub + 4 * s, 4);
        return c;
    }

    return static_cast<int8_t>(ev[rs7ns::kEvChild67 + (s - 6)]);
}

namespace {
// The flag pair of one eval: 0 = neither +64h nor +84h set ("empty"), else the AND of field (when
// +64h) and field + 20h (when +84h), starting from `al`.
bool Empty(const uint8_t* e) {
    return !e[0x64] && !e[0x84];
}

uint8_t Fold(const uint8_t* e, int field, uint8_t al) {
    if (e[0x64]) {
        al &= e[field];
    }
    if (e[0x84]) {
        al &= e[field + 0x20];
    }

    return al;
}
}  // namespace

uint8_t Aggregate(const uint8_t* ev, const uint8_t* tr, int field, bool zeroOnEmpty, const uint8_t* sp) {
    if (!ev || !ev[kEvIsChord]) {
        return 0;
    }

    if (ev[kEvWhole]) {  // the chord as a whole: the parent's own flags
        if (Empty(ev)) {
            return 0;
        }

        return Fold(ev, field, 1) & 1;
    }

    auto childEval = [&](int c) {
        return tr + static_cast<intptr_t>(c) * kEvalSize + 8;
    };

    if (sp) {  // one string's child (stock: strings 0-5 only)
        const int s = *sp;
        if (s >= rs7ns::kSlots) {
            return 0;
        }

        const int c = Child(ev, s);
        if (c < 0) {
            return 0;
        }

        const uint8_t* ce = childEval(c);
        if (Empty(ce)) {
            return 0;
        }

        return Fold(ce, field, 1) & 1;
    }

    uint8_t al = 1;

    for (int s = 0; s < rs7ns::kSlots; ++s) {
        const int c = Child(ev, s);
        if (c < 0) {
            continue;
        }

        const uint8_t* ce = childEval(c);

        if (Empty(ce)) {
            if (zeroOnEmpty) {
                al = 0;
            }
            continue;
        }

        al = Fold(ce, field, al);
    }

    return al & 1;
}

namespace {
int32_t TailMidi(const uint8_t* ev, int t) {
    int32_t m;

    std::memcpy(&m, ev + rs7ns::kEvMidi67 + 4 * t, 4);

    return m;
}

uint32_t TailMask(const uint8_t* ev, int t) {
    uint32_t m;

    std::memcpy(&m, ev + rs7ns::kEvMask67 + 4 * t, 4);

    return m;
}
}  // namespace

bool TailPitched(const uint8_t* ev) {
    bool pitched = false;

    for (int t = 0; t < 2; ++t) {
        pitched |= TailMidi(ev, t) > 0 && !(TailMask(ev, t) & 0x400000);
    }

    return pitched;
}

uint8_t FoldTails(const uint8_t* ev, uint32_t bits, bool byteMask, bool none, uint8_t result, uint8_t* lastTest) {
    for (int t = 0; t < 2; ++t) {
        if (TailMidi(ev, t) <= 0) {
            continue;
        }

        const uint32_t mask = byteMask ? ev[rs7ns::kEvMask67 + 4 * t] : TailMask(ev, t);
        const uint8_t test = ((mask & bits) != 0) != none;

        *lastTest = test;
        result &= test;
    }

    return result;
}

int32_t ExtremeSlide(const uint8_t* ev, int32_t extreme, int32_t* lastDistance) {
    for (int t = 0; t < 2; ++t) {
        const int32_t fret = ev[rs7ns::kEvFret67 + t];

        if (fret == 0) {
            continue;
        }

        const int32_t distance = ev[rs7ns::kEvSlide67 + t] - fret;

        *lastDistance = distance;
        if (extreme == 0) {
            extreme = distance;
        }
        if (extreme < 0 && distance < extreme) {
            extreme = distance;
        }
        if (extreme > 0 && distance > extreme) {
            extreme = distance;
        }
    }

    return extreme;
}

}  // namespace rs7track

extern "C" uint8_t __cdecl Rs7Ns_Aggregate(const uint8_t* eval, const uint8_t* tracker, int field, int zeroOnEmpty,
                                           const uint8_t* stringPtr) {
    return rs7track::Aggregate(eval, tracker, field, zeroOnEmpty != 0, stringPtr);
}
