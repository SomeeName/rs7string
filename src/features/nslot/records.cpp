// The native N-slot chord data's records (nslot.h): T8 / C8 built from the stock template / ChordNotes record and the
// extended columns, and the tracker eval's N-slot tails. Bytes in, bytes out; reads the eight-string preparation
// switches only.
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "features/publish/publish.h"
#include "model/eight.h"
#include <cstring>

namespace rs7ns {

bool DecodeName31(const uint8_t* tpl, Col* out) {
    const uint8_t* name = tpl + 0x28;
    if (!std::memchr(name, 0, 31)) {
        return false;  // a 31-character name: name[31] is text
    }

    const uint8_t b = name[31];
    const int fret = (b & 0x1F) - 1, finger = (b >> 5) - 1;
    if (fret < 0) {
        return false;
    }

    out->fret = fret;
    out->finger = finger;

    return true;
}

void BuildT8(const uint8_t* tpl, const Col ext[2], const int16_t* tuning, int n, uint8_t capo, uint8_t* out) {
    std::memset(out, 0, kT8Size);
    std::memcpy(out, tpl, kTplSize);
    int8_t* frets = reinterpret_cast<int8_t*>(out + kT8Frets);
    int8_t* fingers = reinterpret_cast<int8_t*>(out + kT8Fingers);
    int32_t* midi = reinterpret_cast<int32_t*>(out + kT8Midi);

    for (int s = 0; s < kSlots; ++s) {
        frets[s] = -1;
        fingers[s] = -1;
        midi[s] = -1;
    }

    for (int s = 0; s < 6; ++s) {
        frets[s] = static_cast<int8_t>(tpl[4 + s]);
        fingers[s] = static_cast<int8_t>(tpl[0xA + s]);
        std::memcpy(&midi[s], tpl + 0x10 + 4 * s, 4);
    }

    for (int i = 0; i < 2; ++i) {
        const int s = 6 + i;
        int f = ext[i].fret;
        // Gameplay is still seven-wide; diagrams also need the authored eighth
        // column. Populate it with diagrams or the offline eight preparation switch.
        if (f < 0 || f > 30 || s >= n ||
            (n > kNative && !((g_rs7_diagrams || g_rs7_publish_ports || g_rs7_eight) && n == kSlots))) {
            continue;
        }

        // MIDI from the authored fret (stock template midi[] keeps it too); the fret then follows
        // PostProcessNotes 0x55B4A2, which turns a fret equal to the capo into 0 (frets only).
        midi[s] = kOpenMidi[s] + (tuning && s < n ? tuning[s] : 0) + f;
        if (static_cast<uint8_t>(f) == capo) {
            f = 0;
        }
        frets[s] = static_cast<int8_t>(f);
        // Fingers 0..4 only: the neck finger keep table 0x58C300 has five entries (thumb..pinky).
        fingers[s] = static_cast<int8_t>(ext[i].finger >= -1 && ext[i].finger <= 4 ? ext[i].finger : -1);
    }

    out[kT8Slots] = static_cast<uint8_t>(n > 6 && n <= kSlots ? n : 6);
    uint8_t used = 0;

    for (int s = 0; s < kSlots; ++s) {
        if (frets[s] != -1) {
            used = static_cast<uint8_t>(used | (1u << s));
        }
    }

    out[kT8Used] = used;
}

void BuildC8(const uint8_t* cn, const Tech* tech6, const Tech* tech7, uint8_t* out) {
    std::memset(out, 0, kC8Size);
    std::memcpy(out, cn, kCnSize);
    uint32_t* mask = reinterpret_cast<uint32_t*>(out + kC8Mask);
    int8_t* slide = reinterpret_cast<int8_t*>(out + kC8SlideTo);
    int8_t* unp = reinterpret_cast<int8_t*>(out + kC8SlideUnp);
    int16_t* vib = reinterpret_cast<int16_t*>(out + kC8Vib);

    for (int s = 0; s < kSlots; ++s) {
        slide[s] = -1;
        unp[s] = -1;
    }

    for (int s = 0; s < 6; ++s) {
        std::memcpy(&mask[s], cn + 4 * s, 4);
        slide[s] = static_cast<int8_t>(cn[0x930 + s]);
        unp[s] = static_cast<int8_t>(cn[0x936 + s]);
        std::memcpy(&vib[s], cn + 0x93C + 2 * s, 2);
        std::memcpy(out + kC8Bends + s * kBendSize, cn + 0x18 + s * kBendSize, kBendSize);
    }

    const Tech* tech[2] = {tech6, tech7};

    for (int i = 0; i < 2; ++i) {
        const Tech* t = tech[i];
        if (!t) {
            continue;
        }

        const int s = 6 + i;
        uint32_t m = t->mask;
        int8_t to = t->slideTo, un = t->slideUnpitchTo;

        // SNG_Level_FixupChordNotes 0x55D490: an unpitched slide flagged 0x8000000 without a pitched
        // one becomes a pitched slide (strings 0-5 get this in the stock record).
        if ((m & 0x8000000) && un != -1 && to == -1) {
            to = un;
            un = -1;
            m = (m & ~0x400000u) | 0x800u;
        }

        mask[s] = m;
        slide[s] = to;
        unp[s] = un;
        vib[s] = t->vibrato;
        uint8_t* b = out + kC8Bends + s * kBendSize;
        const int count = t->bendCount < 0 ? 0 : t->bendCount > 32 ? 32 : t->bendCount;

        if (count && t->bends) {
            std::memcpy(b, t->bends, static_cast<size_t>(count) * 12);
        }
        const uint32_t used = static_cast<uint32_t>(count);

        std::memcpy(b + 0x180, &used, 4);
    }
}

bool StringMask8(const uint8_t* c8, uint32_t string, uint32_t flag, uint32_t* bits) {
    if (string >= kSlots) {
        return false;
    }

    *bits = *reinterpret_cast<const uint32_t*>(c8 + kC8Mask + string * 4) & flag;

    return true;
}

FretSpan DoubleStopSpan(uint32_t begin, uint32_t id, uint32_t record) {
    if (record == begin + id * kTplSize) {
        return {record + 4, 6};
    }

    return {record + kT8Frets, kNative};
}

uint32_t SynthMask(uint32_t noteMask, int fret, uint8_t capo) {
    if (fret < 0) {
        return 0;
    }

    uint32_t m = 0;

    if (noteMask & 0x40) {
        m |= 0x40;
    }
    if (noteMask & 8) {
        m |= 0x20000;
    }
    if (noteMask & 0x4000000) {
        m |= 0x4000000;
    }
    if ((fret == capo && capo != 0xFF) || fret == 0) {
        m |= 4;
    }

    return m;
}

}  // namespace rs7ns

namespace rs7ns {

void ClearEvalTails(uint8_t* ev) {
    const int32_t none[2] = {-1, -1};

    std::memcpy(ev + kEvMidi67, none, 8);
    std::memset(ev + kEvMask67, 0, 8);
    std::memset(ev + kEvFret67, 0xFF, 8);  // frets, fingers, slideTo, slideUnpitchTo
    std::memset(ev + kEvVib67, 0, 8);      // vibrato[2] + the last 4 name bytes
}

void WriteEvalTails(uint8_t* ev, const uint8_t* t8, const uint8_t* c8) {
    ClearEvalTails(ev);

    if (t8) {
        std::memcpy(ev + kEvMidi67, t8 + kT8Midi + 6 * 4, 8);
        std::memcpy(ev + kEvFret67, t8 + kT8Frets + 6, 2);
        std::memcpy(ev + kEvFinger67, t8 + kT8Fingers + 6, 2);
    }

    if (c8) {
        std::memcpy(ev + kEvMask67, c8 + kC8Mask + 6 * 4, 8);
        std::memcpy(ev + kEvSlide67, c8 + kC8SlideTo + 6, 2);
        std::memcpy(ev + kEvUnp67, c8 + kC8SlideUnp + 6, 2);
        std::memcpy(ev + kEvVib67, c8 + kC8Vib + 6 * 2, 4);
    }
}

}  // namespace rs7ns
