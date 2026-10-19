// Chord logic: the old M4 template codec (name[31]). Values in, values out.
#include "features/chords/chords.h"
#include <cstring>

namespace rs7chord {

bool Decode(uint8_t b, Ext* out) {
    const int fret = (b & 0x1F) - 1;
    const int finger = (b >> 5) - 1;
    if (fret < 0) {
        return false;  // 0 = no column (a finger without a fret is malformed: ignored too)
    }

    out->fret = fret;
    out->finger = finger;

    return true;
}

uint8_t Encode(int fret, int finger) {
    if (fret < 0 || fret > 30) {
        return 0;
    }

    if (finger < -1 || finger > 6) {
        finger = -1;
    }

    return static_cast<uint8_t>(((finger + 1) << 5) | (fret + 1));
}

bool FromTemplateBytes(const uint8_t* tpl, Ext* out) {
    if (!std::memchr(tpl + kTplName, 0, 31)) {
        return false;  // a 31+ character name: not ours
    }

    return Decode(tpl[kTplExt], out);
}

}  // namespace rs7chord
