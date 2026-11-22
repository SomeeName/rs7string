// Fuzz targets: the native N-slot chord records built from a song's stock template / ChordNotes record and its
// extension block (features/nslot/nslot.h, records.cpp; the chord diagram's view of a T8,
// features/diagrams/diagrams.h), and the plain SNG copy (features/psarc/psarc.h: Rsxr_Stub_PP_SNGD for SNG flags 1).
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "features/psarc/psarc.h"
#include "fuzz.h"
#include "model/extblock.h"
#include <cstring>
#include <vector>

// The game's SNG decrypt replacement (features/psarc/hooks.cpp; not in a header: only the hook table calls it).
extern "C" {
bool __cdecl Rsxr_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);
}

namespace rsxrfuzz {
namespace {
constexpr uint32_t kEvalSize = 0xC28;  // Play_NoteEval (nslot.h)

void Sink(void* ctx, const char* key, int value) {
    *static_cast<size_t*>(ctx) += std::strlen(key) + size_t(value);
}

// The stock six-column projection of a C chord template (frets x 3 2 0 1 0).
std::vector<uint8_t> Template() {
    std::vector<uint8_t> t(rsxrns::kTplSize, 0);
    const int8_t frets[6] = {-1, 3, 2, 0, 1, 0}, fingers[6] = {-1, 3, 2, -1, 1, -1};

    for (int s = 0; s < 6; ++s) {
        t[4 + s] = uint8_t(frets[s]);
        t[0xA + s] = uint8_t(fingers[s]);
    }

    std::memcpy(t.data() + 0x28, "C", 2);

    return t;
}

// The chord diagram of one T8 record, built from it and merged with the template's manifest shape.
void Diagrams(const uint8_t* tpl, const uint8_t* t8, uint8_t capo, size_t* sink) {
    rsxrdiagram::Shape shape, manifest;

    manifest.capo = capo;

    for (int s = 0; s < 6; ++s) {
        manifest.frets[s] = int8_t(tpl[4 + s]);
        manifest.fingers[s] = int8_t(tpl[0xA + s]);
    }

    if (rsxrdiagram::FromT8(t8, &shape)) {
        rsxrdiagram::Emit(shape, Sink, sink);
    }
    if (rsxrdiagram::MergeManifestT8(t8, &manifest)) {
        rsxrdiagram::Emit(manifest, Sink, sink);
    }
}
}  // namespace

// [tuning count % 10][capo][templates % 5][chordNotes % 5][probe][strings % 17][template, 0x48 bytes][extension
// block...] Every template id gets the same stock template; the ChordNotes records are zero. Without a valid block,
// extra chord columns are empty, as at song load.
int Nslot(const uint8_t* d, size_t n) {
    if (n < 6) {
        return 0;
    }

    const uint32_t tuning = d[0] % 10, templates = d[2] % 5, chordNotes = d[3] % 5;
    const uint8_t capo = d[1], probe = d[4];
    const int strings = d[5] % 17;

    d += 6;
    n -= 6;
    uint8_t tpl[rsxrns::kTplSize] = {};
    const size_t tn = n < sizeof tpl ? n : sizeof tpl;

    std::memcpy(tpl, d, tn);
    d += tn;
    n -= tn;
    int16_t tun[16];

    for (int i = 0; i < 16; ++i) {
        tun[i] = int16_t(int8_t(tpl[0x10 + i]) % 25);
    }

    rsxrext::Block block;
    char why[160];
    const bool hasBlock =
        n && rsxrext::Parse(d, n, templates, chordNotes, tuning, &block, why, sizeof why) == rsxrext::Status::Ok;
    std::vector<uint8_t> t8(rsxrns::kT8Size), c8(rsxrns::kC8Size), cn(rsxrns::kCnSize, 0), eval(kEvalSize, 0);
    size_t sink = 0;

    for (uint32_t t = 0; t < (templates ? templates : 1); ++t) {
        rsxrns::Col c[2];

        if (hasBlock) {
            for (int i = 0; i < 2; ++i) {
                int f = 0, g = 0;

                if (block.TemplateColumn(t, 6 + i, &f, &g)) {
                    c[i] = rsxrns::Col{f, g};
                }
            }
        }

        rsxrns::BuildT8(tpl, c, tun, strings, capo, t8.data());
        Diagrams(tpl, t8.data(), capo, &sink);
    }

    for (uint32_t r = 0; r < (chordNotes ? chordNotes : 1); ++r) {
        rsxrns::Tech tech[2];
        const rsxrns::Tech* tp[2] = {nullptr, nullptr};

        for (int i = 0; hasBlock && block.hasChordNotes && i < 2; ++i) {
            const int col = block.Column(6 + i);
            const size_t at = size_t(r) * block.strings.size() + size_t(col);

            if (col < 0 || at >= block.tech.size()) {
                continue;
            }

            const rsxrext::Tech& x = block.tech[at];

            tech[i].mask = x.mask;
            tech[i].slideTo = x.slideTo;
            tech[i].slideUnpitchTo = x.slideUnpitchTo;
            tech[i].vibrato = x.vibrato;
            tech[i].bends = x.bends.empty() ? nullptr : x.bends.data();
            tech[i].bendCount = int(x.bends.size());
            tp[i] = &tech[i];
        }

        rsxrns::BuildC8(cn.data(), tp[0], tp[1], c8.data());
        uint32_t bits = 0;

        rsxrns::StringMask8(c8.data(), probe % 10, 0xFFFFFFFFu, &bits);
        sink += bits;
        rsxrns::WriteEvalTails(eval.data(), t8.data(), c8.data());
    }

    volatile size_t keep = sink;

    (void)keep;

    return 0;
}

Seeds NslotSeeds() {
    const std::vector<uint8_t> tpl = Template();
    std::vector<uint8_t> withoutBlock = {7, 0xFF, 1, 1, 0x40, 7};

    withoutBlock.insert(withoutBlock.end(), tpl.begin(), tpl.end());
    // docs/architecture/extension-block.md "Example" (as in the extblock seeds): 7 strings, two templates, one
    // ChordNotes record.
    const uint8_t block[] = {
        'R', 'S', 'X', 'R', 1, 0, 16, 0, 0x4E, 0,    0,    0,    3,    0,    0,    0,   'S', 'T', 'R', 'S',
        1,   0,   1,   0,   5, 0, 0,  0, 0,    7,    1,    0,    6,    'C',  'H',  'D', 'T', 1,   0,   1,
        0,   8,   0,   0,   0, 2, 0,  0, 0,    2,    1,    0xFF, 0xFF, 'C',  'H',  'N', 'T', 1,   0,   0,
        0,   13,  0,   0,   0, 1, 0,  0, 0,    0x00, 0x20, 0,    0,    0xFF, 0xFF, 0,   0,   0,
    };
    std::vector<uint8_t> withBlock = {7, 0xFF, 2, 1, 0x48, 7};

    withBlock.insert(withBlock.end(), tpl.begin(), tpl.end());
    withBlock.insert(withBlock.end(), block, block + sizeof block);

    return {withoutBlock, withBlock, {6, 0, 0, 0, 0, 8}};
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

    if (RsxrPlain_SngAction(magic, flags, len, outLen) == RsxrSngAction::Decrypt) {
        return 0;
    }

    std::vector<uint8_t> out(outLen ? outLen : 1);

    Rsxr_Stub_PP_SNGD(sng.data() + 8, len, nullptr, 0, out.data(), outLen);

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
}  // namespace rsxrfuzz
