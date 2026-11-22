#include "model/extblock.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace rsxrext;

namespace {
// docs/architecture/extension-block.md "Example" (rsxr-chart tests/test_extblock.py EXAMPLE_HEX): 7 strings, two
// templates (string 6 fret 2 finger 1; none), one ChordNotes record (string 6 mask SUSTAIN).
const uint8_t kExample[] = {
    'R', 'S', 'X', 'R', 1, 0, 16, 0, 0x4E, 0,    0,    0,    3,    0,    0,    0,   'S', 'T', 'R', 'S',
    1,   0,   1,   0,   5, 0, 0,  0, 0,    7,    1,    0,    6,    'C',  'H',  'D', 'T', 1,   0,   1,
    0,   8,   0,   0,   0, 2, 0,  0, 0,    2,    1,    0xFF, 0xFF, 'C',  'H',  'N', 'T', 1,   0,   0,
    0,   13,  0,   0,   0, 1, 0,  0, 0,    0x00, 0x20, 0,    0,    0xFF, 0xFF, 0,   0,   0,
};

std::vector<uint8_t> Ex() {
    return std::vector<uint8_t>(kExample, kExample + sizeof kExample);
}

Status P(const std::vector<uint8_t>& b, Block* out, uint32_t tpl = 2, uint32_t cn = 1, uint32_t tuning = 7) {
    char why[160];

    return Parse(b.data(), b.size(), tpl, cn, tuning, out, why, sizeof why);
}

void PutU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}
}  // namespace

TEST(extblock_example_parses) {
    Block b;

    CHECK(sizeof kExample == 0x4E);
    CHECK(P(Ex(), &b) == Status::Ok);
    CHECK(b.stringCount == 7 && b.Columns() == 1 && b.strings[0] == 6 && b.hasTemplates && b.hasChordNotes);
    int f = 0, g = 0;

    CHECK(b.TemplateColumn(0, 6, &f, &g) && f == 2 && g == 1);
    CHECK(!b.TemplateColumn(1, 6, &f, &g));
    CHECK(!b.TemplateColumn(0, 7, &f, &g));
    CHECK(b.tech.size() == 1 && b.tech[0].mask == 0x2000 && b.tech[0].slideTo == -1 && b.tech[0].bends.empty());
}

TEST(extblock_none_and_not_a_block) {
    Block b;

    CHECK(Parse(nullptr, 0, 0, 0, 7, &b, nullptr, 0) == Status::None);
    const uint8_t junk[] = {0, 0, 0, 0};

    CHECK(Parse(junk, sizeof junk, 0, 0, 7, &b, nullptr, 0) == Status::NotBlock);
}

TEST(extblock_rules_against_the_song) {
    Block b;

    CHECK(P(Ex(), &b, 3) == Status::Invalid);        // CHDT count != chord templates
    CHECK(P(Ex(), &b, 2, 0) == Status::Invalid);     // CHNT count != ChordNotes records
    CHECK(P(Ex(), &b, 2, 1, 6) == Status::Invalid);  // 7 strings but 6 tuning entries
    auto e = Ex();

    e.push_back(0);  // block size != bytes after MetaData
    CHECK(P(e, &b) == Status::Invalid);
    e = Ex();
    e[49] = 25;  // CHDT template 0 fret 25
    CHECK(P(e, &b) == Status::Invalid);
    e = Ex();
    e[51] = 0xFF, e[52] = 2;  // template 1: finger without a fret
    CHECK(P(e, &b) == Status::Invalid);
    e = Ex();
    e[32] = 7;  // STRS column string 7 for a 7-string block
    CHECK(P(e, &b) == Status::Invalid);
    e = Ex();
    e[4] = 2;  // version 2
    CHECK(P(e, &b) == Status::Unsupported);
    e = Ex();
    e[28] = 1;  // instrument 1 (bass) with 7 strings
    CHECK(P(e, &b) == Status::Invalid);
    e[28] = 2;  // an unknown instrument
    CHECK(P(e, &b) == Status::Unsupported);
}

// Extended bass (spec 2.3): STRS only, instrument 1, 5 or 6 strings, no columns, six tuning entries.
TEST(extblock_bass_marks_the_arrangement) {
    const std::vector<uint8_t> bass = {'R', 'S', 'X', 'R', 1, 0, 16, 0, 32, 0, 0, 0, 1, 0, 0, 0,
                                       'S', 'T', 'R', 'S', 1, 0, 1,  0, 4,  0, 0, 0, 1, 5, 0, 0};
    Block b;

    CHECK(P(bass, &b, 0, 0, 6) == Status::Ok);
    CHECK(b.instrument == kInstrumentBass && b.stringCount == 5 && b.Columns() == 0 && !b.hasTemplates);
    auto e = bass;

    e[29] = 6;
    CHECK(P(e, &b, 0, 0, 6) == Status::Ok && b.stringCount == 6);
    e[29] = 4;  // a stock 4-string bass carries no block
    CHECK(P(e, &b, 0, 0, 6) == Status::Invalid);
    e[29] = 7;
    CHECK(P(e, &b, 0, 0, 6) == Status::Invalid);
    CHECK(P(bass, &b, 0, 0, 7) == Status::Invalid);  // seven tuning entries
    e = bass;
    e[30] = 1;  // a column
    CHECK(P(e, &b, 0, 0, 6) == Status::Invalid);
}

TEST(extblock_unknown_sections) {
    Block b;
    auto e = Ex();

    // CHNT -> ZZZZ, not critical: skipped (no extended techniques)
    std::memcpy(e.data() + 53, "ZZZZ", 4);
    CHECK(P(e, &b) == Status::Ok && b.skipped == 1 && !b.hasChordNotes);
    e[53 + 6] = 1;  // critical
    CHECK(P(e, &b) == Status::Unsupported);
    e = Ex();
    e[37] = 2;  // CHDT v2 (critical): unsupported
    CHECK(P(e, &b) == Status::Unsupported);
    e = Ex();
    std::memcpy(e.data() + 16, "CHDT", 4);  // first section is not STRS
    CHECK(P(e, &b) == Status::Invalid);
    e = Ex();
    PutU32(e, 12, 4);  // section count past the end
    CHECK(P(e, &b) == Status::Invalid);
}
