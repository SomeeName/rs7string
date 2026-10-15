#include "core/chord7.h"
#include "core/vguit.h"
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

TEST(vguit_chord6_parse) {
    using namespace rs7vg;
    Chord6 m = Chord6::Play;
    int sh = 0;

    CHECK(ParseChord6(L"omit", &m, &sh) && m == Chord6::Omit);
    CHECK(ParseChord6(L" Only ", &m, &sh) && m == Chord6::Only);
    CHECK(ParseChord6(L"play", &m, &sh) && m == Chord6::Play);
    CHECK(ParseChord6(L"+1", &m, &sh) && m == Chord6::Shift && sh == 1);
    CHECK(ParseChord6(L"-12", &m, &sh) && m == Chord6::Shift && sh == -12);
    CHECK(!ParseChord6(L"+0", &m, &sh));
    CHECK(!ParseChord6(L"+30", &m, &sh));
    CHECK(!ParseChord6(L"3", &m, &sh));
    CHECK(!ParseChord6(L"x", &m, &sh));
}
