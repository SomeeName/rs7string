#include "features/headstock/assetload.h"
#include "test.h"
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <string>
#include <vector>

TEST(assetload_timed_out_builder_does_not_publish_its_path) {
    volatile long state = 1;  // a waiter exhausted its budget while the builder is still running
    std::wstring path = L"C:\\headstock\\still-changing.nif";

    CHECK(Rs7Asset_PublishedPath(path, &state).empty());
    path = L"C:\\headstock\\finished.nif";
    _InterlockedExchange(&state, 2);
    CHECK(Rs7Asset_PublishedPath(path, &state) == path);
    path.clear();  // a completed but failed builder must keep the stock fallback
    CHECK(Rs7Asset_PublishedPath(path, &state).empty());
}

TEST(assetload_edited_path_of_a_guitar_model) {
    const std::wstring dir = L"C:\\edited\\";

    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif", dir,
                              L"_7inline") == L"C:\\edited\\epi_wilshire_cherry_7inline.nif");
    // No trailing separator, upper case and backslashes in the game name.
    CHECK(Rs7Asset_EditedPath("data\\Assets\\Gameplay\\Guitar_Standard\\GIB_LesPaul.NIF", L"D:\\x", L"_7") ==
          L"D:\\x\\gib_lespaul_7.nif");
}

TEST(assetload_ignores_everything_but_guitar_nifs) {
    const std::wstring dir = L"C:\\edited\\";

    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/inlay/skull.nif", dir, L"_7inline").empty());
    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/guitar_standard_inline/wilshire.dds", dir, L"_7inline").empty());
    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/guitar_x/.nif", dir, L"_7inline").empty());
    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/guitar_x/a b.nif", dir, L"_7inline").empty());
    CHECK(Rs7Asset_EditedPath("data/assets/gameplay/guitar_x/foo.nif", L"", L"_7inline").empty());
    CHECK(Rs7Asset_EditedPath(nullptr, dir, L"_7inline").empty());
}

TEST(assetload_7string_model_applies_to_the_inline_right_hand_folder) {
    CHECK(Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif"));
    CHECK(Rs7Asset_IsRhCandidate("DATA\\Assets\\Gameplay\\Guitar_Standard_Inline\\Other.NIF"));
    CHECK(!Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_standard_inline/epi_whilshire_cherry_lh.nif"));
    CHECK(!Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_bass_inline/bass_thunder.nif"));
    CHECK(!Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_bass/bass_jackcasady.nif"));
    CHECK(!Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_standard_inline/sub/x.nif"));
    CHECK(!Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_standard_inline/wilshire.dds"));
    CHECK(!Rs7Asset_IsRhCandidate(nullptr));
}

TEST(assetload_lefty_model_applies_to_the_inline_left_hand_frame) {
    CHECK(Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_standard_inline/epi_whilshire_cherry_lh.nif"));
    CHECK(Rs7Asset_IsLhCandidate("DATA\\Assets\\Gameplay\\Guitar_Standard_Inline\\Other_LH.NIF"));
    CHECK(!Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif"));
    CHECK(!Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_bass_inline/bass_thunder_lh.nif"));
    CHECK(!Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_standard_inline/sub/x_lh.nif"));
    CHECK(!Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_standard_inline/wilshire_lh.dds"));
    CHECK(!Rs7Asset_IsLhCandidate("_lh.nif"));
    CHECK(!Rs7Asset_IsLhCandidate(nullptr));
    CHECK(rs7asset::kDefaultFile7Lh == std::wstring(L"rs7_inline7_lh.nif"));
}

// Gitarrenkopfplatte = "3+3": the game loads guitar_standard/gib_lespaul.nif (pegs peg_bakelite.nif). Since
// fix/headstock2 the 7-string model replaces it, and peg_bakelite.nif gets the in-line peg merged in.
TEST(assetload_3plus3_is_a_layout_row) {
    bool lh = true;
    const HeadLayout* l = Rs7Asset_LayoutOf("data/assets/gameplay/guitar_standard/gib_lespaul.nif", &lh);

    CHECK(l != nullptr && std::strcmp(l->option, "3+3") == 0 && !lh);
    CHECK(std::strcmp(l->stockRh, "LP_RH") == 0 && std::strcmp(l->stockLh, "LP_LH") == 0);
    l = Rs7Asset_LayoutOf("data/assets/gameplay/guitar_standard/gib_lespaul_lh.nif", &lh);
    CHECK(l != nullptr && lh);
    CHECK(Rs7Asset_IsRhCandidate("data/assets/gameplay/guitar_standard/gib_lespaul.nif"));
    CHECK(Rs7Asset_IsLhCandidate("data/assets/gameplay/guitar_standard/gib_lespaul_lh.nif"));
    l = Rs7Asset_LayoutOf("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif", &lh);
    CHECK(l != nullptr && std::strcmp(l->option, "6 in Reihe") == 0 && l->peg == nullptr);
    CHECK(Rs7Asset_LayoutOf("data/assets/gameplay/guitar_standard_inline2/epi_wilshire_cherry.nif", &lh) == nullptr);
    CHECK(Rs7Asset_LayoutOf("data/assets/gameplay/guitar_standard2/gib_lespaul.nif", &lh) == nullptr);
}

TEST(assetload_layout_peg) {
    CHECK(Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard/peg_bakelite.nif"));
    CHECK(Rs7Asset_IsLayoutPeg("DATA\\Assets\\Gameplay\\Guitar_Standard\\PEG_Bakelite.NIF"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard/gib_lespaul.nif"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard_inline/epi_whilshire_peg.nif"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard_inline/peg_bakelite.nif"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard/sub/peg_bakelite.nif"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard/peg_bakelite.dds"));
    CHECK(!Rs7Asset_IsLayoutPeg("data/assets/gameplay/guitar_standard/peg_bakelite2.nif"));
    CHECK(!Rs7Asset_IsLayoutPeg(nullptr));
}

// LT1: with no head showing the 7-string model the letter rule gets the InstrumentInfo's own in-line byte.
TEST(assetload_letter_inline_own_byte) {
    uint8_t info[0x38] = {};

    CHECK(Rs7Asset_LetterInline(info) == 0);
    info[0x0A] = 1;
    CHECK(Rs7Asset_LetterInline(info) == 1);
    CHECK(Rs7Asset_LetterInline(nullptr) == 0);
}

// PGH1: a peg-pass mesh is hidden only inside a culled part next to rs7_alt (or rs7_alt itself).
namespace {
struct PegNode {
    alignas(4) uint8_t b[0xE0] = {};

    void Name(const char* n) { std::memcpy(b + 0x10, &n, 4); }

    void Cull(bool on) { b[0x20] = on ? 1 : 0; }

    void Parent(PegNode& p) {
        uint8_t* pp = p.b;

        std::memcpy(b + 0x24, &pp, 4);
    }

    void Kids(uint8_t** kids, uint16_t n) {
        std::memcpy(b + 0xD8, &kids, 4);
        std::memcpy(b + 0xDC, &n, 2);
    }
};
}  // namespace

TEST(assetload_peg_mesh_hidden_only_in_a_swapped_part) {
    PegNode root, stock, alt, stockMesh, altMesh, other, otherMesh;
    uint8_t* kids[3] = {stock.b, alt.b, other.b};

    root.Name("Scene Root");
    root.Kids(kids, 3);
    stock.Name("peg_bakelite");
    alt.Name("rs7_alt");
    other.Name("locator_letter");

    for (PegNode* n : {&stock, &alt, &other}) {
        n->Parent(root);
    }

    stockMesh.Parent(stock);
    altMesh.Parent(alt);
    otherMesh.Parent(other);

    alt.Cull(true);  // stock peg shown: the in-line knob is the hidden part
    CHECK(!Rs7Asset_PegMeshHidden(stockMesh.b));
    CHECK(Rs7Asset_PegMeshHidden(altMesh.b));
    alt.Cull(false);
    stock.Cull(true);  // swapped: the bakelite knob is hidden
    CHECK(Rs7Asset_PegMeshHidden(stockMesh.b));
    CHECK(!Rs7Asset_PegMeshHidden(altMesh.b));
    CHECK(!Rs7Asset_PegMeshHidden(otherMesh.b));
    root.Cull(true);  // culled without an rs7_alt sibling (a stock hide): drawn as before
    CHECK(Rs7Asset_PegMeshHidden(stockMesh.b));
    stock.Cull(false);
    CHECK(!Rs7Asset_PegMeshHidden(stockMesh.b));
    CHECK(!Rs7Asset_PegMeshHidden(nullptr));
}

TEST(assetload_headstock_layout_table) {
    CHECK(kLayoutCount == 2);

    for (int i = 0; i < kLayoutCount; ++i) {
        const HeadLayout& l = kLayouts[i];

        CHECK(l.option && l.option[0] && l.stockRh && l.stockLh);
        const size_t n = std::strlen(l.folder);

        CHECK(n > 2 && l.folder[0] == '/' && l.folder[n - 1] == '/');  // matched with the slash on both sides

        for (int j = 0; j < i; ++j) {
            CHECK(std::strcmp(kLayouts[j].folder, l.folder) != 0);
        }

        // The stock parts name a Wilshire / Les Paul tree, one per handedness.
        CHECK(std::strstr(l.stockRh, "RH") != nullptr && std::strstr(l.stockLh, "LH") != nullptr);
    }

    CHECK(std::strcmp(kLayouts[0].stockRh, "Wilshire_RH") == 0 && std::strcmp(kLayouts[0].stockLh, "Wilshire_LH") == 0);
    CHECK(kLayouts[0].peg == nullptr && kLayouts[1].peg && std::strcmp(kLayouts[1].peg, "peg_bakelite") == 0);
}

TEST(assetload_resolve_file_value) {
    CHECK(Rs7Asset_ResolveFile(L"C:\\own\\rs7_inline7.nif", L"D:\\e\\") == L"C:\\own\\rs7_inline7.nif");
    CHECK(Rs7Asset_ResolveFile(L"  own\\rs7_inline7.nif ", L"D:\\e") == L"D:\\e\\own\\rs7_inline7.nif");
    CHECK(Rs7Asset_ResolveFile(L"\\\\server\\x.nif", L"D:\\e\\") == L"\\\\server\\x.nif");

    for (const wchar_t* off : {L"", L"  ", L"none", L"NONE", L"off", L"0"}) {
        CHECK(Rs7Asset_ResolveFile(off, L"D:\\e\\").empty());
    }
}

TEST(assetload_dds_names_of_a_nif) {
    // NIF string table style: u32 length + text, plus a path and a near miss.
    const char raw[] = "\x0e\0\0\0rs7hs_wood.dds\x10\0\0\0cubemap_tail.DDS\x05\0\0\0x.ddsx"
                       "assets/gameplay/foo/rs7hs_env.dds\0rs7hs_wood.dds\0.dds\0";
    const auto n = Rs7Asset_DdsNames(reinterpret_cast<const uint8_t*>(raw), sizeof raw - 1);

    CHECK_EQ(int(n.size()), 3);
    CHECK(n.size() == 3 && n[0] == "rs7hs_wood.dds" && n[1] == "cubemap_tail.DDS" && n[2] == "rs7hs_env.dds");
    CHECK(Rs7Asset_DdsNames(nullptr, 10).empty());
}

TEST(assetload_folder_of_a_resource_name) {
    CHECK(Rs7Asset_FolderOf("data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif") ==
          "data/assets/gameplay/guitar_standard_inline/");
    CHECK(Rs7Asset_FolderOf("x.nif").empty());
    CHECK(Rs7Asset_FolderOf(nullptr).empty());
}

TEST(assetload_dxt1_darkening_keeps_every_block_mode) {
    std::vector<uint8_t> d(128 + 4 * 8, 0);

    std::memcpy(d.data(), "DDS ", 4);
    std::memcpy(d.data() + 84, "DXT1", 4);
    // block 0: 4-colour, white / grey; block 1: 3-colour (c0 <= c1); block 2: 4-colour, close end points;
    // block 3: 4-colour, red vs green (u16 order by red)
    const uint16_t cs[4][2] = {{0xFFFF, 0x8410}, {0x0010, 0x0020}, {0x0841, 0x0840}, {0x0800, 0x07E0}};

    for (int b = 0; b < 4; ++b) {
        uint8_t* blk = d.data() + 128 + 8 * b;

        blk[0] = uint8_t(cs[b][0]);
        blk[1] = uint8_t(cs[b][0] >> 8);
        blk[2] = uint8_t(cs[b][1]);
        blk[3] = uint8_t(cs[b][1] >> 8);
        blk[4] = 0xE4;
        blk[5] = 0xE4;
        blk[6] = 0xE4;
        blk[7] = 0xE4;  // indices 0,1,2,3
    }

    CHECK(Rs7Asset_ScaleDxt1(d.data(), d.size(), 0.5f));

    for (int b = 0; b < 4; ++b) {
        const uint8_t* blk = d.data() + 128 + 8 * b;
        const uint16_t n0 = uint16_t(blk[0] | (blk[1] << 8)), n1 = uint16_t(blk[2] | (blk[3] << 8));
        const bool four = cs[b][0] > cs[b][1];
        const bool allZero = blk[4] == 0 && blk[5] == 0 && blk[6] == 0 && blk[7] == 0;

        CHECK(four ? (n0 > n1 || allZero) : n0 <= n1);
    }

    const uint8_t* w = d.data() + 128;  // white -> about half: r 31 -> 16

    CHECK(((w[0] | (w[1] << 8)) >> 11) == 16);
    std::vector<uint8_t> bad(200, 0);

    CHECK(!Rs7Asset_ScaleDxt1(bad.data(), bad.size(), 0.5f));
    CHECK(!Rs7Asset_ScaleDxt1(d.data(), d.size(), 1.0f));
}

TEST(assetload_wood_colour_texture_names) {
    CHECK(Rs7Asset_IsWoodColour("rs7hs_wood.dds"));
    CHECK(Rs7Asset_IsWoodColour("RS7HS_WOOD.DDS"));
    CHECK(!Rs7Asset_IsWoodColour("rs7hs_wood_gloss.dds"));
    CHECK(!Rs7Asset_IsWoodColour("rs7hs_hardware.dds"));
    CHECK(!Rs7Asset_IsWoodColour("wood_n.dds"));
}

TEST(assetload_default_dir_is_relative_to_the_dll_folder) {
    // The built-in dir is rs7string\headstock next to the DLL (the game folder); the 7-string model is in it.
    const std::wstring dllDir = L"C:\\Games\\Rocksmith2014\\";
    const std::wstring dir = Rs7Asset_ResolveDir(L"", dllDir);

    CHECK(dir == L"C:\\Games\\Rocksmith2014\\rs7string\\headstock\\");
    CHECK(Rs7Asset_ResolveFile(rs7asset::kDefaultFile7, dir) ==
          L"C:\\Games\\Rocksmith2014\\rs7string\\headstock\\rs7_inline7.nif");
    // The overlay writes the table's default text back into the INI: the same folder.
    CHECK(Rs7Asset_ResolveDir(L"rs7string\\headstock\\", dllDir) == dir);
    // Absolute values are kept; a relative value without a DLL folder stays relative.
    CHECK(Rs7Asset_ResolveDir(L"D:\\models", dllDir) == L"D:\\models");
    CHECK(Rs7Asset_ResolveDir(L"models\\", L"") == L"models\\");
    // The built-in defaults name no user profile.
    CHECK(std::wcsstr(rs7asset::kDefaultDir, L"Users") == nullptr);
    CHECK(std::wcsstr(rs7asset::kDefaultFile7, L"Users") == nullptr);
}

namespace {
// Fake NiAVObject property lists: obj +B8h -> node {next, ?, prop}; the test's type query reads prop +4.
struct FakeProp {
    uint32_t vt;
    int type;
    uint8_t pad[0x18];
    uint16_t layer;
    uint16_t rest;
};

struct FakeNode {
    FakeNode* next;
    void* unused;
    FakeProp* prop;
};

struct FakeObj {
    uint8_t head[0xB8];
    FakeNode* props;
};

int FakeType(const uint8_t* prop) {
    return reinterpret_cast<const FakeProp*>(prop)->type;
}
}  // namespace

TEST(assetload_layer_property_offsets_match_the_game) {  // 0x534CE0..: type 0xC, u16 at +20h
    static_assert(offsetof(FakeProp, layer) == rs7asset::kLayerValue, "fake layout");
    static_assert(offsetof(FakeObj, props) == rs7asset::kPropList, "fake layout");

    CHECK(rs7asset::kLayerType == 0xC);
}

TEST(assetload_string_size_7_gets_string_size_6s_draw_layer) {  // issue #23
    FakeProp shade6{0, 0x8, {}, 0, 0}, layer6{0, 0xC, {}, 11, 0};
    FakeProp shade7{0, 0x8, {}, 99, 0}, alpha7{0, 0x0, {}, 98, 0}, layer7{0, 0xC, {}, 1, 0};
    FakeNode n6b{nullptr, nullptr, &layer6}, n6a{&n6b, nullptr, &shade6};
    FakeNode n7c{nullptr, nullptr, &layer7}, n7b{&n7c, nullptr, &alpha7}, n7a{&n7b, nullptr, nullptr};
    FakeNode n7{&n7a, nullptr, &shade7};
    FakeObj seg6{}, seg7{};

    seg6.props = &n6a;
    seg7.props = &n7;
    CHECK(rs7asset::FindProperty(reinterpret_cast<uint8_t*>(&seg7), 0xC, FakeType) ==
          reinterpret_cast<uint8_t*>(&layer7));
    uint16_t value = 0, was = 0;

    CHECK(rs7asset::CopyLayer(reinterpret_cast<uint8_t*>(&seg6), reinterpret_cast<uint8_t*>(&seg7), FakeType, &value,
                              &was) == 1);
    CHECK(value == 11 && was == 1 && layer7.layer == 11);
    CHECK(shade7.layer == 99 && alpha7.layer == 98 && layer6.layer == 11);  // nothing else written
    CHECK(rs7asset::CopyLayer(reinterpret_cast<uint8_t*>(&seg6), reinterpret_cast<uint8_t*>(&seg7), FakeType, &value,
                              &was) == 0);
}

TEST(assetload_draw_layer_copy_needs_both_layer_properties) {
    FakeProp shade{0, 0x8, {}, 5, 0}, layer{0, 0xC, {}, 11, 0};
    FakeNode ns{nullptr, nullptr, &shade}, nl{nullptr, nullptr, &layer};
    FakeObj noLayer{}, withLayer{}, empty{};

    noLayer.props = &ns;
    withLayer.props = &nl;
    uint16_t value = 0, was = 7;

    CHECK(rs7asset::CopyLayer(reinterpret_cast<uint8_t*>(&withLayer), reinterpret_cast<uint8_t*>(&noLayer), FakeType,
                              &value, &was) == -1);
    CHECK(shade.layer == 5 && was == 7);
    CHECK(rs7asset::CopyLayer(reinterpret_cast<uint8_t*>(&noLayer), reinterpret_cast<uint8_t*>(&withLayer), FakeType,
                              &value, &was) == -1);
    CHECK(layer.layer == 11);
    CHECK(rs7asset::CopyLayer(nullptr, reinterpret_cast<uint8_t*>(&withLayer), FakeType, &value, &was) == -1);
    CHECK(rs7asset::FindProperty(reinterpret_cast<uint8_t*>(&empty), 0xC, FakeType) == nullptr);
    CHECK(rs7asset::FindProperty(nullptr, 0xC, FakeType) == nullptr);
    CHECK(rs7asset::GamePropType(nullptr) == -1);
}

// Review #14: the right-handed and left-handed jobs share one owner of the common build output.
#include <windows.h>

namespace {
volatile long g_builds = 0, g_inside = 0, g_overlap = 0;

bool SlowBuild(void*) {
    if (_InterlockedIncrement(&g_inside) > 1) {
        _InterlockedIncrement(&g_overlap);
    }
    _InterlockedIncrement(&g_builds);
    Sleep(300);
    _InterlockedDecrement(&g_inside);

    return true;
}

bool FailingBuild(void*) {
    _InterlockedIncrement(&g_builds);

    return false;
}

bool ThrowingBuild(void*) {
    throw 1;
}

struct Caller {
    volatile long* state;
    bool (*build)(void*);
    bool result;
};

DWORD WINAPI CallOnce(LPVOID p) {
    auto* c = static_cast<Caller*>(p);

    c->result = Rs7Asset_RunOnce(c->state, c->build, nullptr, 5000);

    return 0;
}
}  // namespace

TEST(assetload_shared_build_runs_once_for_concurrent_left_and_right_jobs) {
    volatile long state = 0;

    g_builds = g_inside = g_overlap = 0;
    Caller a{&state, SlowBuild, false}, b{&state, SlowBuild, false};
    HANDLE t[2] = {CreateThread(nullptr, 0, CallOnce, &a, 0, nullptr),
                   CreateThread(nullptr, 0, CallOnce, &b, 0, nullptr)};

    WaitForMultipleObjects(2, t, TRUE, 10000);
    CloseHandle(t[0]);
    CloseHandle(t[1]);
    CHECK_EQ(g_builds, 1L);  // one owner wrote the files
    CHECK_EQ(g_overlap, 0L);
    CHECK(a.result && b.result);                             // the waiter reuses the owner's result
    CHECK(Rs7Asset_RunOnce(&state, SlowBuild, nullptr, 0));  // later callers reuse it too, no rebuild
    CHECK_EQ(g_builds, 1L);
}

TEST(assetload_shared_build_failure_is_shared_and_contained) {
    volatile long state = 0;

    g_builds = 0;
    CHECK(!Rs7Asset_RunOnce(&state, FailingBuild, nullptr, 0));
    CHECK(!Rs7Asset_RunOnce(&state, FailingBuild, nullptr, 0));  // the waiter does not retry or write
    CHECK_EQ(g_builds, 1L);
    volatile long thrown = 0;

    CHECK(!Rs7Asset_RunOnce(&thrown, ThrowingBuild, nullptr, 0));  // no exception escapes, state leaves "building"
    CHECK_EQ(long(thrown), 3L);
    volatile long stuck = 1;  // owner still building: the waiter times out, builds nothing

    g_builds = 0;
    CHECK(!Rs7Asset_RunOnce(&stuck, SlowBuild, nullptr, 100));
    CHECK_EQ(g_builds, 0L);
}
