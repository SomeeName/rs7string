#include "core/diagrammovie.h"
#include "core/diagrams.h"
#include "test.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> Bytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);

    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

TEST(diagrammovie_paths_are_exact_case_insensitive_and_support_data_prefix) {
    CHECK_EQ(rs7diagram::MovieTarget("DATA\\GFXASSETS\\LIBS\\CommonLib.GFX"), 0);
    CHECK_EQ(rs7diagram::MovieTarget("gfxassets/views/contentpanel.gfx"), 1);
    CHECK_EQ(rs7diagram::MovieTarget("data/gfxassets/views/ge_game.gfx"), 2);
    CHECK_EQ(rs7diagram::MovieTarget("other/gfxassets/libs/commonlib.gfx"), -1);
    CHECK_EQ(rs7diagram::MovieTarget("gfxassets/libs/commonlib.gfx.old"), -1);
    CHECK_EQ(rs7diagram::MovieTarget(nullptr), -1);
}

TEST(diagrammovie_cpp_swap_matches_all_three_owner_movies_and_refuses_tampering) {
    wchar_t exe[MAX_PATH];

    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto repo = std::filesystem::path(exe).parent_path().parent_path().parent_path();
    wchar_t home[MAX_PATH];

    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    auto root = std::filesystem::path(home) / L"Desktop" / L"rocksmith-assets";
    const char* const paths[] = {"libs/commonlib.gfx", "views/contentpanel.gfx", "views/ge_game.gfx"};
    const char* const recipes[] = {"commonlib.rs7da", "contentpanel.rs7da", "ge_game.rs7da"};
    const uint16_t sprites[] = {1103, 28, 26};

    for (int i = 0; i < 3; ++i) {
        auto original = Bytes(root / "static.psarc" / "gfxassets" / paths[i]);
        auto expected = Bytes(root / "edited2" / "gfx" / paths[i]);

        if (original.empty() || expected.empty()) {
            std::printf("  SKIP absent owner movies: %s\n", paths[i]);
            continue;
        }

        auto recipe = Bytes(repo / "data" / "chord-diagrams" / recipes[i]);
        std::vector<uint8_t> out{42};

        CHECK(rs7diagram::SwapMovie(original.data(), original.size(), recipe.data(), recipe.size(), sprites[i], &out));
        CHECK(out == expected);

        for (auto malformed : {expected, std::vector<uint8_t>{1, 2, 3}}) {
            auto saved = out;

            CHECK(!rs7diagram::SwapMovie(malformed.data(), malformed.size(), recipe.data(), recipe.size(), sprites[i],
                                         &out));
            CHECK(saved == out);
        }

        auto r = recipe;

        r[20] ^= 1;
        CHECK(!rs7diagram::SwapMovie(original.data(), original.size(), r.data(), r.size(), sprites[i], &out));
        r = recipe;
        r[52] ^= 1;
        CHECK(!rs7diagram::SwapMovie(original.data(), original.size(), r.data(), r.size(), sprites[i], &out));
        CHECK(!rs7diagram::SwapMovie(original.data(), original.size(), recipe.data(), recipe.size() - 1, sprites[i],
                                     &out));
        auto compressed = original;

        compressed[0] = 'C';
        CHECK(!rs7diagram::SwapMovie(compressed.data(), compressed.size(), recipe.data(), recipe.size(), sprites[i],
                                     &out));
        std::printf("  %s C++ byte-identical (%zu bytes)\n", paths[i], expected.size());
    }
}

// ---- FileOpener hook frame (vtable 0x121F2C4 slot 3: thiscall, 4 stack args, ret 0x10) ----
namespace {
struct FakeFile {
    const void* const* vt;
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;
    int released = 0;
};

LONG __fastcall FfRel(FakeFile* f, void*) {
    return ++f->released;
}

uint32_t __fastcall FfRead(FakeFile* f, void*, void* p, uint32_t n) {
    n = std::min<uint32_t>(n, uint32_t(f->bytes.size()) - f->pos);
    std::memcpy(p, f->bytes.data() + f->pos, n);
    f->pos += n;

    return n;
}

uint32_t __fastcall FfSize(FakeFile* f, void*) {
    return uint32_t(f->bytes.size());
}

uint32_t __fastcall FfTell(FakeFile* f, void*) {
    return f->pos;
}

bool __fastcall FfSeek(FakeFile* f, void*, uint32_t p) {
    if (p > f->bytes.size()) {
        return false;
    }

    f->pos = p;

    return true;
}

const void* const kFfVt[] = {nullptr,        nullptr,        (void*)&FfRel, (void*)&FfRead,
                             (void*)&FfSize, (void*)&FfTell, (void*)&FfSeek};

struct Wrapper {
    uintptr_t vt;
    uint32_t refs;
    uint8_t* adapter;
};

struct Opened {
    void* self;
    const char* url;
    void* heap;
    int flags;
    int mode;
    int calls = 0;
    void* ret = nullptr;
};

Opened g_opened;

void* __fastcall FakeOpen(void* self, void*, const char* url, void* heap, int flags, int mode) {
    g_opened.self = self;
    g_opened.url = url;
    g_opened.heap = heap;
    g_opened.flags = flags;
    g_opened.mode = mode;
    ++g_opened.calls;

    return g_opened.ret;
}

using OpenCall = void*(__thiscall*)(void*, const char*, void*, int, int);
}  // namespace

TEST(diagrammovie_open_hook_frame_failure_paths_leave_wrapper_untouched) {
    auto hook = reinterpret_cast<OpenCall>(rs7diagram::OpenHookForTest());

    rs7diagram::SetOpenForTest(reinterpret_cast<void*>(&FakeOpen));
    const uintptr_t saved = g_rs7_diagrams;

    g_rs7_diagrams = 1;
    // recipe present but garbage movie -> SwapMovie refuses
    rs7diagram::SetRecipeForTest(0, std::vector<uint8_t>(100, 7));
    FakeFile ff{kFfVt, std::vector<uint8_t>(64, 1)};
    uint8_t adapter[0x1c] = {};

    {
        FakeFile* pf = &ff;

        std::memcpy(adapter + 0x18, &pf, 4);
    }

    adapter[4] = 9;
    adapter[0x10] = 1;
    Wrapper w{0x121F748, 1, adapter};
    int dummy;

    // non-target url: original called with all four args, result returned, nothing swapped
    g_opened.ret = &w;
    void* r = hook((void*)0x1234, "data\\gfxassets\\views\\other.gfx", &dummy, 0x21, 0x1B6);

    CHECK(r == &w);
    CHECK_EQ(g_opened.calls, 1);
    CHECK(g_opened.self == (void*)0x1234);
    CHECK(g_opened.heap == &dummy);
    CHECK_EQ(g_opened.flags, 0x21);
    CHECK_EQ(g_opened.mode, 0x1B6);
    // target url, unusable movie
    r = hook((void*)0x1234, "data\\gfxassets\\libs\\CommonLib.gfx", &dummy, 0x21, 0x1B6);
    CHECK(r == &w);
    CHECK_EQ(g_opened.calls, 2);
    CHECK(*reinterpret_cast<FakeFile**>(adapter + 0x18) == &ff);
    CHECK_EQ(ff.released, 0);
    CHECK_EQ(adapter[4], 9);
    CHECK_EQ(ff.pos, 0u);
    // null wrapper from the opener
    g_opened.ret = nullptr;
    CHECK(hook((void*)0x1234, "data\\gfxassets\\libs\\CommonLib.gfx", &dummy, 0x21, 0x1B6) == nullptr);
    // adapter / IFile missing
    g_opened.ret = &w;
    w.adapter = nullptr;
    CHECK(hook((void*)0x1234, "data\\gfxassets\\libs\\CommonLib.gfx", &dummy, 0x21, 0x1B6) == &w);
    w.adapter = adapter;
    *reinterpret_cast<void**>(adapter + 0x18) = nullptr;
    CHECK(hook((void*)0x1234, "data\\gfxassets\\libs\\CommonLib.gfx", &dummy, 0x21, 0x1B6) == &w);
    // diagrams7=0: nothing is touched even for a target url
    *reinterpret_cast<FakeFile**>(adapter + 0x18) = &ff;
    g_rs7_diagrams = 0;
    CHECK(hook((void*)0x1234, "data\\gfxassets\\libs\\CommonLib.gfx", &dummy, 0x21, 0x1B6) == &w);
    CHECK(*reinterpret_cast<FakeFile**>(adapter + 0x18) == &ff);
    const char* why = nullptr;

    rs7diagram::SetRecipeForTest(0, {});
    CHECK(!rs7diagram::SwapWrapperFile(&w, "data\\gfxassets\\libs\\CommonLib.gfx", &why));
    CHECK(why != nullptr);
    g_rs7_diagrams = saved;
    rs7diagram::SetOpenForTest(nullptr);
}

TEST(diagrammovie_open_hook_installs_owned_file_for_owner_movie) {
    wchar_t exe[MAX_PATH];

    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto repo = std::filesystem::path(exe).parent_path().parent_path().parent_path();
    wchar_t home[MAX_PATH];

    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    auto root = std::filesystem::path(home) / L"Desktop" / L"rocksmith-assets";
    auto original = Bytes(root / "static.psarc" / "gfxassets" / "libs/commonlib.gfx");
    auto expected = Bytes(root / "edited2" / "gfx" / "libs/commonlib.gfx");

    if (original.empty() || expected.empty()) {
        std::printf("  SKIP absent owner movie\n");
        return;
    }

    auto hook = reinterpret_cast<OpenCall>(rs7diagram::OpenHookForTest());

    rs7diagram::SetOpenForTest(reinterpret_cast<void*>(&FakeOpen));
    const uintptr_t saved = g_rs7_diagrams;

    g_rs7_diagrams = 1;
    rs7diagram::SetRecipeForTest(0, Bytes(repo / "data" / "chord-diagrams" / "commonlib.rs7da"));
    FakeFile ff{kFfVt, original, 0};
    uint8_t adapter[0x1c] = {};
    FakeFile* p = &ff;

    std::memcpy(adapter + 0x18, &p, 4);
    adapter[4] = 5;
    adapter[0x10] = 1;
    Wrapper w{0x121F748, 1, adapter};

    g_opened.ret = &w;
    CHECK(hook((void*)1, "data\\gfxassets\\libs\\CommonLib.gfx", nullptr, 0x21, 0x1B6) == &w);
    CHECK_EQ(ff.released, 1);
    CHECK_EQ(adapter[4], 0);
    CHECK_EQ(adapter[0x10], 1);
    auto* nf = *reinterpret_cast<void***>(adapter + 0x18);

    CHECK(static_cast<void*>(nf) != static_cast<void*>(&ff));
    auto vt = *reinterpret_cast<void***>(nf);
    auto size = reinterpret_cast<uint32_t(__thiscall*)(void*)>(vt[4])(nf);

    CHECK_EQ(size, uint32_t(expected.size()));
    std::vector<uint8_t> got(size);

    CHECK_EQ(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, uint32_t)>(vt[3])(nf, got.data(), size), size);
    CHECK(got == expected);
    reinterpret_cast<LONG(__thiscall*)(void*)>(vt[2])(nf);  // owned file starts at refs=1 -> freed
    g_rs7_diagrams = saved;
    rs7diagram::SetOpenForTest(nullptr);
    rs7diagram::SetRecipeForTest(0, {});
}
