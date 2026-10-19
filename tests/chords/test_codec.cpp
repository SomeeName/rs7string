#include "features/chords/chords.h"
#include "test.h"
#include <cstring>

using namespace rs7chord;

TEST(chord7_codec) {
    Ext x;

    CHECK(!Decode(0, &x));
    CHECK(Encode(2, 1) == ((2 << 5) | 3));
    CHECK(Decode(Encode(2, 1), &x) && x.fret == 2 && x.finger == 1);
    CHECK(Decode(Encode(0, -1), &x) && x.fret == 0 && x.finger == -1);
    CHECK(Encode(0, -1) == 1);
    CHECK(Decode(Encode(30, 4), &x) && x.fret == 30 && x.finger == 4);
    CHECK(Encode(31, 1) == 0);
    CHECK(Encode(-1, 1) == 0);
    CHECK(Decode(Encode(5, 9), &x) && x.fret == 5 && x.finger == -1);  // bad finger -> none
    CHECK(!Decode(0x40, &x));                                          // finger without fret
}

TEST(chord7_template_bytes) {
    uint8_t t[kTplSize] = {};

    std::memcpy(t + kTplName, "C#5", 4);
    Ext x;

    CHECK(!FromTemplateBytes(t, &x));
    t[kTplExt] = Encode(2, 1);
    CHECK(FromTemplateBytes(t, &x) && x.fret == 2 && x.finger == 1);
    std::memset(t + kTplName, 'A', 31);  // a 31-character name: name[31] is part of the text
    CHECK(!FromTemplateBytes(t, &x));
}
