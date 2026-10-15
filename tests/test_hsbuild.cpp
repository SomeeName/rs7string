#include "core/hsbuild.h"
#include "core/hsbuild_io.h"
#include "test.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
rs7hs::Bytes Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

rs7hs::Bytes Package(const char* name = "rs7string_headstock.bin") {
    wchar_t exe[MAX_PATH] = {};

    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto repo = std::filesystem::path(exe).parent_path().parent_path().parent_path();

    return Read(repo / "data/headstock" / name);
}

std::filesystem::path AssetsBase() {
    wchar_t home[MAX_PATH] = {};

    GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);

    return std::filesystem::path(home) / L"Desktop" / L"rocksmith-assets";
}

const std::filesystem::path kBase = AssetsBase();
const auto kTemplates = kBase / "base/dependency_guitarstandardinlinedrh.psarc/assets/gameplay/guitar_standard_inline";
const auto kTemplatesLh =
    kBase / "base/dependency_guitarstandardinlinedlh.psarc/assets/gameplay/guitar_standard_inline";

void U32(rs7hs::Bytes& b, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
        b.push_back(uint8_t(n >> (i * 8)));
    }
}

void Digest(rs7hs::Bytes& b, const rs7hs::Bytes& source) {
    const auto hex = rs7hs::Hash(source);

    for (size_t i = 0; i < hex.size(); i += 2) {
        b.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
}

rs7hs::Bytes Encode(const rs7hs::Bytes& b) {
    rs7hs::Bytes result{'R', 'S', '7', 'H', '1', '\n'};

    for (auto v : b) {
        result.push_back("0123456789abcdef"[v >> 4]);
        result.push_back("0123456789abcdef"[v & 15]);
    }

    return result;
}

struct Tiny {
    rs7hs::Bytes raw;
    size_t firstSize = 0, firstHash = 0, firstOp = 0;

    Tiny() {
        U32(raw, 1);
        Digest(raw, {'x'});
        Digest(raw, {'y'});
        U32(raw, 9);

        for (const char* name :
             {"rs7_inline7.nif", "rs7_inline8.nif", "rs7_peg.nif", "rs7hs_env.dds", "rs7hs_flat_n.dds",
              "rs7hs_hardware.dds", "rs7hs_hardware_gloss.dds", "rs7hs_wood.dds", "rs7hs_wood_gloss.dds"}) {
            const std::string s = name;

            U32(raw, uint32_t(s.size()));
            raw.insert(raw.end(), s.begin(), s.end());
            const bool first = firstOp == 0;

            if (first) {
                firstSize = raw.size();
            }
            U32(raw, 1);
            if (first) {
                firstHash = raw.size();
            }
            Digest(raw, {'x'});
            U32(raw, 1);

            if (first) {
                firstOp = raw.size();
                raw.push_back(1);
                U32(raw, 0);
                U32(raw, 0);
                U32(raw, 1);
            } else {
                raw.push_back(0);
                U32(raw, 1);
                raw.push_back('x');
            }
        }
    }
};
}  // namespace

TEST(hsbuild_sha256_standard_vectors) {
    CHECK_EQ(rs7hs::Hash({}), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(rs7hs::Hash({'a', 'b', 'c'}), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(rs7hs::Hash(rs7hs::Bytes(1000000, 'a')),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(hsbuild_owner_byte_identity_and_hash_refusal) {
    const auto guitar = Read(kTemplates / "epi_wilshire_cherry.nif");
    const auto peg = Read(kTemplates / "epi_whilshire_peg.nif");

    if (guitar.empty() || peg.empty()) {
        std::puts("SKIP hsbuild owner identity: templates absent");
        return;
    }

    const auto package = Package();

    CHECK(!package.empty());
    if (package.empty()) {
        return;
    }

    std::vector<rs7hs::File> outputs;
    std::string error;

    CHECK(rs7hs::Build(guitar, peg, package, outputs, error));
    CHECK_EQ(outputs.size(), 9u);

    for (const auto& f : outputs) {
        const auto expectedPath = kBase / "edited/own" / f.name;

        CHECK(std::filesystem::exists(expectedPath));
        CHECK_EQ(f.bytes, Read(expectedPath));
        std::printf("hsbuild byte-identity %s: %zu bytes\n", f.name.c_str(), f.bytes.size());
    }

    auto wrong = guitar;

    wrong[0] ^= 1;
    CHECK(!rs7hs::Build(wrong, peg, package, outputs, error));
    CHECK(outputs.empty());
    CHECK_EQ(error, "template hash mismatch");
    wrong = peg;
    wrong.back() ^= 1;
    CHECK(!rs7hs::Build(guitar, wrong, package, outputs, error));
    CHECK_EQ(error, "template hash mismatch");
    auto corrupt = package;

    corrupt.back() = corrupt.back() == '0' ? '1' : '0';
    CHECK(!rs7hs::Build(guitar, peg, corrupt, outputs, error));
    CHECK(outputs.empty());
}

TEST(hsbuild_package_manifest_and_malformed_refusal) {
    auto package = Package();

    CHECK(!package.empty());
    if (package.empty()) {
        return;
    }

    std::vector<rs7hs::Expected> manifest;
    std::string error;

    CHECK(rs7hs::Manifest(package, manifest, error));
    CHECK_EQ(manifest.size(), 9u);

    for (const auto& f : manifest) {
        CHECK_EQ(f.sha256.size(), 64u);
    }

    for (size_t n : {size_t(0), size_t(5), size_t(10), size_t(200)}) {
        CHECK(!rs7hs::Manifest(rs7hs::Bytes(package.begin(), package.begin() + n), manifest, error));
        CHECK(manifest.empty());
    }

    package.push_back('0');
    CHECK(!rs7hs::Manifest(package, manifest, error));
}

TEST(hsbuild_synthetic_copy_insert_and_parser_bounds) {
    Tiny tiny;
    std::vector<rs7hs::File> outputs;
    std::vector<rs7hs::Expected> manifest;
    std::string error;

    CHECK(rs7hs::Build({'x'}, {'y'}, Encode(tiny.raw), outputs, error));
    CHECK_EQ(outputs.size(), 9u);

    for (const auto& f : outputs) {
        CHECK_EQ(f.bytes, rs7hs::Bytes{'x'});
    }

    CHECK(rs7hs::CheckTemplates({'x'}, {'y'}, Encode(tiny.raw), error));
    CHECK(!rs7hs::CheckTemplates({'z'}, {'y'}, Encode(tiny.raw), error));
    CHECK_EQ(error, "template hash mismatch");

    for (const auto position : {tiny.firstOp, tiny.firstOp + 1, tiny.firstOp + 5, tiny.firstOp + 9, tiny.firstSize}) {
        auto corrupt = tiny.raw;

        corrupt[position] = 255;
        CHECK(!rs7hs::Build({'x'}, {'y'}, Encode(corrupt), outputs, error));
        CHECK(outputs.empty());
    }

    auto corrupt = tiny.raw;

    corrupt[tiny.firstHash] ^= 1;
    CHECK(!rs7hs::Build({'x'}, {'y'}, Encode(corrupt), outputs, error));
    CHECK_EQ(error, "output hash mismatch");
    corrupt = tiny.raw;
    corrupt.push_back(0);
    CHECK(!rs7hs::Manifest(Encode(corrupt), manifest, error));
    CHECK_EQ(error, "trailing package bytes");
    corrupt = tiny.raw;
    corrupt[0] = 2;
    CHECK(!rs7hs::Manifest(Encode(corrupt), manifest, error));
    corrupt = tiny.raw;
    corrupt[76] = '/';  // First name must be one of the fixed basenames.
    CHECK(!rs7hs::Manifest(Encode(corrupt), manifest, error));
    CHECK_EQ(error, "artifact name/order");
    auto encoded = Encode(tiny.raw);

    encoded[7] = 'z';
    CHECK(!rs7hs::Manifest(encoded, manifest, error));
    CHECK_EQ(error, "package hex digit");
}

namespace {
bool ReadXY(rs7hs::Bytes& g, rs7hs::Bytes& p) {
    g = {'x'};
    p = {'y'};

    return true;
}

bool ReadBad(rs7hs::Bytes& g, rs7hs::Bytes& p) {
    g = {'z'};
    p = {'y'};

    return true;
}

bool ReadNone(rs7hs::Bytes& g, rs7hs::Bytes& p) {
    g.clear();
    p.clear();

    return false;
}
}  // namespace

TEST(hsbuild_ensure_builds_caches_and_falls_back) {
    Tiny tiny;
    const auto package = Encode(tiny.raw);
    wchar_t tmp[MAX_PATH] = {};

    GetTempPathW(MAX_PATH, tmp);
    const auto dir = std::filesystem::path(tmp) / ("rs7_selfbuild_test_" + std::to_string(GetCurrentProcessId()));

    std::filesystem::remove_all(dir);
    std::string note;

    // game reader fails / unknown templates: stock stays, nothing written
    CHECK(Rs7Hs_Ensure(package, dir, ReadNone, false, note) == Rs7HsResult::Failed);
    CHECK(Rs7Hs_Ensure(package, dir, ReadBad, false, note) == Rs7HsResult::Failed);
    CHECK_EQ(note, "template hash mismatch; no output touched");
    CHECK(!std::filesystem::exists(dir / "rs7_inline7.nif"));
    // first run builds all nine; the second needs no reader at all
    CHECK(Rs7Hs_Ensure(package, dir, ReadXY, false, note) == Rs7HsResult::Built);
    CHECK_EQ(note, "verified build, wrote 9 files");
    CHECK_EQ(Read(dir / "rs7hs_wood.dds"), rs7hs::Bytes{'x'});
    CHECK(Rs7Hs_Ensure(package, dir, ReadNone, false, note) == Rs7HsResult::Cached);
    CHECK(Rs7Hs_Ensure(package, dir, ReadNone, true, note) == Rs7HsResult::Cached);
    // a missing file is rebuilt; an edited one is kept unless replacing is requested
    std::filesystem::remove(dir / "rs7hs_env.dds");

    {
        std::ofstream o(dir / "rs7_peg.nif", std::ios::binary);

        o << "edited";
    }

    CHECK(Rs7Hs_Ensure(package, dir, ReadXY, false, note) == Rs7HsResult::Built);
    CHECK_EQ(note, "verified build, wrote 1 files");
    CHECK_EQ(Read(dir / "rs7_peg.nif"), (rs7hs::Bytes{'e', 'd', 'i', 't', 'e', 'd'}));
    CHECK(Rs7Hs_Ensure(package, dir, ReadXY, true, note) == Rs7HsResult::Built);
    CHECK_EQ(Read(dir / "rs7_peg.nif"), rs7hs::Bytes{'x'});
    // a damaged package never touches the folder
    auto bad = tiny.raw;

    bad[0] = 2;
    CHECK(Rs7Hs_Ensure(Encode(bad), dir / "other", ReadXY, false, note) == Rs7HsResult::Failed);
    CHECK(!std::filesystem::exists(dir / "other"));
    std::filesystem::remove_all(dir);
}

// Left-handed package: one artifact (the mirrored model), built from the left-handed frame + the peg, byte-identical
// to the offline build; the right-handed templates are refused, and the 9-file package is not accepted in its place.
TEST(hsbuild_lefty_owner_byte_identity_and_refusal) {
    const auto package = Package("rs7string_headstock_lh.bin");

    CHECK(!package.empty());
    std::vector<rs7hs::Expected> manifest;
    std::string error;

    CHECK(rs7hs::Manifest(package, manifest, error));
    CHECK_EQ(manifest.size(), 1u);
    if (manifest.size() == 1) {
        CHECK_EQ(manifest[0].name, std::string("rs7_inline7_lh.nif"));
    }
    const auto guitar = Read(kTemplatesLh / "epi_whilshire_cherry_lh.nif");
    const auto peg = Read(kTemplatesLh / "epi_whilshire_peg.nif");

    if (guitar.empty() || peg.empty()) {
        std::puts("SKIP hsbuild lefty owner identity: templates absent");
        return;
    }

    std::vector<rs7hs::File> outputs;

    CHECK(rs7hs::Build(guitar, peg, package, outputs, error));
    CHECK_EQ(outputs.size(), 1u);

    for (const auto& f : outputs) {
        CHECK_EQ(f.bytes, Read(kBase / "edited/own" / f.name));
        std::printf("hsbuild lefty byte-identity %s: %zu bytes\n", f.name.c_str(), f.bytes.size());
    }

    const auto rhGuitar = Read(kTemplates / "epi_wilshire_cherry.nif");

    CHECK(!rs7hs::Build(rhGuitar, peg, package, outputs, error));
    CHECK_EQ(error, "template hash mismatch");
    CHECK(!rs7hs::Build(guitar, peg, Package(), outputs, error));
    CHECK_EQ(error, "template hash mismatch");
}

TEST(hsbuild_preserves_unreadable_existing_outputs) {
    Tiny tiny;
    wchar_t tmp[MAX_PATH] = {};

    GetTempPathW(MAX_PATH, tmp);
    const auto dir = std::filesystem::path(tmp) / ("rs7_inputs_preserve_" + std::to_string(GetCurrentProcessId()));

    std::filesystem::create_directories(dir);
    const auto model = dir / "rs7_inline7.nif";

    {
        std::ofstream out(model, std::ios::binary);
    }

    std::string note;

    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, false, note) == Rs7HsResult::Failed);
    CHECK_EQ(std::filesystem::file_size(model), 0u);
    CHECK(!std::filesystem::exists(dir / "rs7_inline8.nif"));
    std::filesystem::remove(model);
    std::filesystem::create_directory(model);
    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, false, note) == Rs7HsResult::Failed);
    CHECK(std::filesystem::is_directory(model));
    CHECK(!std::filesystem::exists(dir / "rs7_inline8.nif"));
    std::filesystem::remove_all(dir);
    // A missing early artifact must not hide an unreadable later artifact during preflight.
    std::filesystem::create_directories(dir);

    {
        std::ofstream out(dir / "rs7hs_wood_gloss.dds", std::ios::binary);
    }

    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, false, note) == Rs7HsResult::Failed);
    CHECK(!std::filesystem::exists(dir / "rs7_inline7.nif"));
    std::filesystem::remove_all(dir);
}

TEST(hsbuild_input_truncation_and_mutation_sweep) {
    Tiny tiny;
    const auto good = Encode(tiny.raw);
    std::vector<rs7hs::Expected> manifest;
    std::vector<rs7hs::File> outputs;
    std::string error;

    for (size_t n = 0; n < good.size(); ++n) {
        const rs7hs::Bytes prefix(good.begin(), good.begin() + n);

        manifest = {{"stale", 1, "stale"}};
        outputs = {{"stale", {'x'}}};
        CHECK(!rs7hs::Manifest(prefix, manifest, error));
        CHECK(manifest.empty());
        CHECK(!error.empty());
        CHECK(!rs7hs::Build({'x'}, {'y'}, prefix, outputs, error));
        CHECK(outputs.empty());
        CHECK(!error.empty());
    }

    uint32_t seed = 0x7c1b4u;

    for (unsigned i = 0; i < 2048; ++i) {
        auto raw = tiny.raw;

        seed = seed * 1664525u + 1013904223u;
        raw[seed % raw.size()] ^= uint8_t(1u << ((seed >> 16) & 7));
        const auto encoded = Encode(raw);
        const bool parsed = rs7hs::Manifest(encoded, manifest, error);

        CHECK(parsed ? manifest.size() == 9u : manifest.empty());
        const bool built = rs7hs::Build({'x'}, {'y'}, encoded, outputs, error);

        CHECK(built ? outputs.size() == 9u : outputs.empty());

        if (built) {
            CHECK(parsed);

            for (const auto& f : outputs) {
                CHECK_EQ(f.bytes, rs7hs::Bytes{'x'});
            }
        }
    }
}

TEST(hsbuild_unicode_cache_and_failed_write_preserve_outputs) {
    Tiny tiny;
    wchar_t tmp[MAX_PATH] = {};

    GetTempPathW(MAX_PATH, tmp);
    const auto dir =
        std::filesystem::path(tmp) / (L"rs7_inputs_\u6f22\u00e4_" + std::to_wstring(GetCurrentProcessId()));

    std::filesystem::create_directories(dir);
    std::string note;

    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, false, note) == Rs7HsResult::Built);
    const auto model = dir / "rs7_inline7.nif";

    CHECK(SetFileAttributesW(model.c_str(), FILE_ATTRIBUTE_READONLY));
    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadNone, false, note) == Rs7HsResult::Cached);
    CHECK(SetFileAttributesW(model.c_str(), FILE_ATTRIBUTE_NORMAL));

    {
        std::ofstream out(model, std::ios::binary);

        out << "custom";
    }

    CHECK(SetFileAttributesW(model.c_str(), FILE_ATTRIBUTE_READONLY));
    CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, true, note) == Rs7HsResult::Failed);
    CHECK_EQ(Read(model), (rs7hs::Bytes{'c', 'u', 's', 't', 'o', 'm'}));
    CHECK(!std::filesystem::exists(std::filesystem::path(model.wstring() + L".building")));
    CHECK(SetFileAttributesW(model.c_str(), FILE_ATTRIBUTE_NORMAL));
    // A sharing violation must not be treated as an absent file either.
    const auto lock =
        CreateFileW(model.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    CHECK(lock != INVALID_HANDLE_VALUE);

    if (lock != INVALID_HANDLE_VALUE) {
        CHECK(Rs7Hs_Ensure(Encode(tiny.raw), dir, ReadXY, false, note) == Rs7HsResult::Failed);
        CloseHandle(lock);
    }

    CHECK_EQ(Read(model), (rs7hs::Bytes{'c', 'u', 's', 't', 'o', 'm'}));
    std::filesystem::remove_all(dir);
}
