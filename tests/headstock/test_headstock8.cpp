// The 8-peg headstock ([neck] eight=1): model choice, PSL1 load count, the raised peg count, the extra segments
// shown per guitar and the extra pegs' colour palette.
#include "features/colours/colours.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_head.h"
#include "features/headstock/pegcolour.h"
#include "features/headstock/pegset.h"
#include "test.h"
#include <cstring>
#include <cwchar>
#include <vector>

using namespace rs7head;

namespace {
// A loaded native PegSet whose slots 6.. up to `ready` are taken, with an entity and a locator.
std::vector<uint8_t> LoadedPegSet(unsigned loaded, unsigned ready) {
    std::vector<uint8_t> m(kSize, 0);
    static uint8_t entity[4], locator[4];

    m[kTail + 1] = uint8_t(loaded);

    for (unsigned s = kStockSlots; s < ready; ++s) {
        uint8_t* slot = Slot(m.data(), int(s));
        uint8_t* e = entity;
        uint8_t* l = locator;

        slot[0] = 1;
        std::memcpy(slot + kSlotEntity, &e, sizeof e);
        std::memcpy(slot + kSlotLocator, &l, sizeof l);
    }

    return m;
}
}  // namespace

TEST(headstock8_model_files_follow_eight) {
    const rs7asset::ModelFiles seven = rs7asset::ModelFilesFor(false);
    const rs7asset::ModelFiles eight = rs7asset::ModelFilesFor(true);

    CHECK(!std::wcscmp(seven.key, L"headstock7_file"));
    CHECK(!std::wcscmp(seven.keyLh, L"headstock7_file_lh"));
    CHECK(!std::wcscmp(seven.file, L"rs7_inline7.nif"));
    CHECK(!std::wcscmp(seven.fileLh, L"rs7_inline7_lh.nif"));
    CHECK_EQ(seven.pegs, 7u);
    CHECK(!std::wcscmp(eight.key, L"headstock8_file"));
    CHECK(!std::wcscmp(eight.keyLh, L"headstock8_file_lh"));
    CHECK(!std::wcscmp(eight.file, L"rs7_inline8.nif"));
    CHECK(!std::wcscmp(eight.fileLh, L"rs7_inline8_lh.nif"));
    CHECK_EQ(eight.pegs, 8u);
}

TEST(headstock8_load_count_seats_the_eighth_peg_only_on_the_8_peg_model) {
    CHECK_EQ(LoadCount(8, true, true, 7, 8, 1), 8u);
    CHECK_EQ(LoadCount(8, true, true, 7, 7, 1), 7u);   // the 7-peg model: no Locator_Peg_8
    CHECK_EQ(LoadCount(7, true, true, 7, 8, 1), 7u);   // a 7-string guitar on the 8-peg model
    CHECK_EQ(LoadCount(6, true, true, 7, 8, 1), 7u);   // headstock_peg: peg 7 loaded, culled
    CHECK_EQ(LoadCount(6, true, true, 7, 8, 2), 6u);   // six-string bass: only its six pooled pegs
    CHECK_EQ(LoadCount(6, true, false, 7, 8, 1), 6u);  // headstock_peg=0
    CHECK_EQ(LoadCount(4, true, true, 7, 8, 2), 4u);   // bass
    CHECK_EQ(LoadCount(9, true, true, 7, 12, 1), 8u);  // never past the native capacity
    CHECK_EQ(LoadCount(8, false, true, 7, 8, 1), 8u);  // a stock PegSet: the stock count
}

TEST(headstock8_ready_count_raises_every_consecutive_seated_extra_peg) {
    std::vector<uint8_t> eight = LoadedPegSet(8, 8);
    std::vector<uint8_t> seven = LoadedPegSet(7, 8);
    std::vector<uint8_t> gap = LoadedPegSet(8, 7);
    std::vector<uint8_t> none = LoadedPegSet(8, 6);

    CHECK_EQ(ReadyCount(eight.data()), 8u);
    CHECK_EQ(ReadyCount(seven.data()), 7u);  // slot 7 seated but not loaded
    CHECK_EQ(ReadyCount(gap.data()), 7u);    // no Locator_Peg_8: peg 8 stays hidden
    CHECK_EQ(ReadyCount(none.data()), 6u);
    std::memset(Slot(eight.data(), 6) + kSlotLocator, 0, sizeof(void*));
    CHECK_EQ(ReadyCount(eight.data()), 6u);  // consecutive only: no peg 8 without peg 7
}

TEST(headstock8_extra_segments_follow_the_guitars_string_count) {
    CHECK(!rs7asset::ExtraShown(0, 6));
    CHECK(rs7asset::ExtraShown(0, 7));
    CHECK(!rs7asset::ExtraShown(1, 7));  // the 8-peg model on a 7-string guitar: string_size_8 culled
    CHECK(rs7asset::ExtraShown(1, 8));
}

TEST(headstock8_bass_segments_use_the_models_names) {
    const int names[6] = {10, 9, 8, 7, 11, 6};  // E A D G, the low B, the high C

    for (int s = 0; s < 6; ++s) {
        CHECK_EQ(rs7asset::BassSegName(s), names[s]);
    }
}

TEST(headstock8_extra_peg_palette_is_the_eighth_string_colour) {
    float buf[6] = {};
    const uint32_t saved[2] = {rs7colour::Colour8(0), rs7colour::Colour8(1)};
    const uint32_t rgb[2] = {0xFF8000, 0x0000FF};

    rs7colour::SetPalette8(rgb);
    CHECK(rs7pegcolour::ExtraPalette(6, buf) == g_rs7_fx_rgb);
    CHECK(rs7pegcolour::ExtraPalette(7, buf) == buf);
    CHECK_EQ(buf[0], 1.0f);
    CHECK(buf[1] > 0.50f && buf[1] < 0.51f);
    CHECK_EQ(buf[2], 0.0f);
    CHECK_EQ(buf[3], 0.0f);
    CHECK_EQ(buf[5], 1.0f);
    rs7colour::SetPalette8(saved);
}
