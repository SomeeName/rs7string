// Fuzz targets: the native N-slot chord records built from a song's stock template / ChordNotes record and its RS7X
// block (features/nslot/nslot.h, records.cpp; the chord diagram's view of a T8, features/diagrams/diagrams.h), and the
// plain SNG copy (features/psarc/psarc.h: Rs7_Stub_PP_SNGD for SNG flags 1).
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "features/psarc/psarc.h"
#include "fuzz.h"
#include "model/extblock.h"
#include <cstring>
#include <vector>

// The game's SNG decrypt replacement (features/psarc/hooks.cpp; not in a header: only the hook table calls it).
extern "C" {
bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);
}

namespace rs7fuzz {
namespace {
constexpr uint32_t kEvalSize = 0xC28;  // Play_NoteEval (nslot.h)

void Sink(void* ctx, const char* key, int value) {
    *static_cast<size_t*>(ctx) += std::strlen(key) + size_t(value);
}

// A seven-string C chord template (frets x 3 2 0 1 0, low B fret 2 by name[31]: finger 2, fret 2).
std::vector<uint8_t> Template() {
    std::vector<uint8_t> t(rs7ns::kTplSize, 0);
    const int8_t frets[6] = {-1, 3, 2, 0, 1, 0}, fingers[6] = {-1, 3, 2, -1, 1, -1};

    for (int s = 0; s < 6; ++s) {
        t[4 + s] = uint8_t(frets[s]);
        t[0xA + s] = uint8_t(fingers[s]);
    }

    std::memcpy(t.data() + 0x28, "C", 2);
    t[0x28 + 31] = uint8_t((2 + 1) << 5 | (2 + 1));

    return t;
}

// The chord diagram of one T8 record, built from it and merged with the template's manifest shape.
void Diagrams(const uint8_t* tpl, const uint8_t* t8, uint8_t capo, size_t* sink) {
    rs7diagram::Shape shape, manifest;

    manifest.capo = capo;

    for (int s = 0; s < 6; ++s) {
        manifest.frets[s] = int8_t(tpl[4 + s]);
        manifest.fingers[s] = int8_t(tpl[0xA + s]);
    }

    if (rs7diagram::FromT8(t8, &shape)) {
        rs7diagram::Emit(shape, Sink, sink);
    }
    if (rs7diagram::MergeManifestT8(t8, &manifest)) {
        rs7diagram::Emit(manifest, Sink, sink);
    }
}
}  // namespace

// [tuning count % 10][capo][templates % 5][chordNotes % 5][probe][strings % 17][template, 0x48 bytes][RS7X block...]
// Every template id gets the same stock template; the ChordNotes records are zero. Without a valid block the
// old name[31] column is used (7 strings), as the song load does.
int Nslot(const uint8_t* d, size_t n) {
    if (n < 6) {
        return 0;
    }

    const uint32_t tuning = d[0] % 10, templates = d[2] % 5, chordNotes = d[3] % 5;
    const uint8_t capo = d[1], probe = d[4];
    const int strings = d[5] % 17;

    d += 6;
    n -= 6;
    uint8_t tpl[rs7ns::kTplSize] = {};
    const size_t tn = n < sizeof tpl ? n : sizeof tpl;

    std::memcpy(tpl, d, tn);
    d += tn;
    n -= tn;
    int16_t tun[16];

    for (int i = 0; i < 16; ++i) {
        tun[i] = int16_t(int8_t(tpl[0x10 + i]) % 25);
    }

    rs7ext::Block block;
    char why[160];
    const bool hasBlock =
        n && rs7ext::Parse(d, n, templates, chordNotes, tuning, &block, why, sizeof why) == rs7ext::Status::Ok;
    std::vector<uint8_t> t8(rs7ns::kT8Size), c8(rs7ns::kC8Size), cn(rs7ns::kCnSize, 0), eval(kEvalSize, 0);
    size_t sink = 0;

    for (uint32_t t = 0; t < (templates ? templates : 1); ++t) {
        rs7ns::Col c[2];

        if (hasBlock) {
            for (int i = 0; i < 2; ++i) {
                int f = 0, g = 0;

                if (block.TemplateColumn(t, 6 + i, &f, &g)) {
                    c[i] = rs7ns::Col{f, g};
                }
            }
        } else if (strings == 7) {
            rs7ns::DecodeName31(tpl, &c[0]);
        }

        rs7ns::BuildT8(tpl, c, tun, strings, capo, t8.data());
        Diagrams(tpl, t8.data(), capo, &sink);
    }

    for (uint32_t r = 0; r < (chordNotes ? chordNotes : 1); ++r) {
        rs7ns::Tech tech[2];
        const rs7ns::Tech* tp[2] = {nullptr, nullptr};

        for (int i = 0; hasBlock && block.hasChordNotes && i < 2; ++i) {
            const int col = block.Column(6 + i);
            const size_t at = size_t(r) * block.strings.size() + size_t(col);

            if (col < 0 || at >= block.tech.size()) {
                continue;
            }

            const rs7ext::Tech& x = block.tech[at];

            tech[i].mask = x.mask;
            tech[i].slideTo = x.slideTo;
            tech[i].slideUnpitchTo = x.slideUnpitchTo;
            tech[i].vibrato = x.vibrato;
            tech[i].bends = x.bends.empty() ? nullptr : x.bends.data();
            tech[i].bendCount = int(x.bends.size());
            tp[i] = &tech[i];
        }

        if (!tp[0] && !hasBlock) {  // a synthesized record (Fixup): the mask rule over the template's string 6
            tech[0].mask = rs7ns::SynthMask(probe, int8_t(t8[rs7ns::kT8Frets + 6]), capo);
            tp[0] = &tech[0];
        }

        rs7ns::BuildC8(cn.data(), tp[0], tp[1], c8.data());
        uint32_t bits = 0;

        rs7ns::StringMask8(c8.data(), probe % 10, 0xFFFFFFFFu, &bits);
        sink += bits;
        rs7ns::WriteEvalTails(eval.data(), t8.data(), c8.data());
    }

    volatile size_t keep = sink;

    (void)keep;

    return 0;
}

Seeds NslotSeeds() {
    const std::vector<uint8_t> tpl = Template();
    std::vector<uint8_t> name31 = {6, 0xFF, 1, 1, 0x40, 7};

    name31.insert(name31.end(), tpl.begin(), tpl.end());
    // docs/release/RS7X-SPEC.md "Example" (as in the rs7x seeds): 7 strings, two templates, one ChordNotes record.
    const uint8_t block[] = {
        'R', 'S', '7', 'X', 1, 0, 16, 0, 0x4E, 0,    0,    0,    3,    0,    0,    0,   'S', 'T', 'R', 'S',
        1,   0,   1,   0,   5, 0, 0,  0, 0,    7,    1,    0,    6,    'C',  'H',  'D', 'T', 1,   0,   1,
        0,   8,   0,   0,   0, 2, 0,  0, 0,    2,    1,    0xFF, 0xFF, 'C',  'H',  'N', 'T', 1,   0,   0,
        0,   13,  0,   0,   0, 1, 0,  0, 0,    0x00, 0x20, 0,    0,    0xFF, 0xFF, 0,   0,   0,
    };
    std::vector<uint8_t> withBlock = {7, 0xFF, 2, 1, 0x48, 7};

    withBlock.insert(withBlock.end(), tpl.begin(), tpl.end());
    withBlock.insert(withBlock.end(), block, block + sizeof block);

    return {name31, withBlock, {6, 0, 0, 0, 0, 8}};
}

// [outLen lo][outLen hi][SNG header: u32 magic, u32 flags][IV slot, 16 bytes][payload...]: the plain SNG copy. The
// game's own decrypt (SNG flags other than 1) is not called: it is game code.
int Plain(const uint8_t* d, size_t n) {
    if (n < 10) {
        return 0;
    }

    const uint32_t outLen = uint32_t(d[0] | d[1] << 8);
    // The game's buffer: header in front of src, exactly len bytes after it (ASan sees any read past either end).
    const std::vector<uint8_t> sng(d + 2, d + n);
    const uint32_t len = uint32_t(sng.size() - 8);
    uint32_t magic, flags;

    std::memcpy(&magic, sng.data(), 4);
    std::memcpy(&flags, sng.data() + 4, 4);

    if (Rs7Plain_SngAction(magic, flags, len, outLen) == Rs7SngAction::Decrypt) {
        return 0;
    }

    std::vector<uint8_t> out(outLen ? outLen : 1);

    Rs7_Stub_PP_SNGD(sng.data() + 8, len, nullptr, 0, out.data(), outLen);

    return 0;
}

Seeds PlainSeeds() {
    std::vector<uint8_t> s = {0x20, 0, 0x4A, 0, 0, 0, 1, 0, 0, 0};

    s.insert(s.end(), 16, 0);  // IV slot

    for (int i = 0; i < 0x20; ++i) {
        s.push_back(uint8_t(i));
    }

    std::vector<uint8_t> small = s;

    small[0] = 0x10;  // out smaller than the payload: Reject

    return {s, small, {0, 0, 0x4A, 0, 0, 0, 1, 0, 0, 0}};
}
}  // namespace rs7fuzz
