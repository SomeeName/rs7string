// Fuzz targets: files rs7string reads from disk besides the ini: the headstock build package (features/headstock
// hsbuild.h), texture names in a NIF and the DXT1 darkening of a .dds (assetload.h), and a chord-diagram recipe
// against a movie (features/diagrams/diagrams.h; the fuzz build skips the recipe's SHA-256 checks, see movie.cpp).
#include "features/diagrams/diagrams.h"
#include "features/headstock/assetload.h"
#include "features/headstock/hsbuild.h"
#include "fuzz.h"
#include <cstring>

namespace rs7fuzz {
int Package(const uint8_t* d, size_t n) {
    const rs7hs::Bytes package(d, d + n);
    std::vector<rs7hs::Expected> outputs;
    std::string error;

    rs7hs::Manifest(package, outputs, error);

    return 0;
}

int Nif(const uint8_t* d, size_t n) {
    Rs7Asset_DdsNames(n ? d : nullptr, n);

    return 0;
}

// [k][dds...]
int Dds(const uint8_t* d, size_t n) {
    if (n < 1) {
        return 0;
    }

    std::vector<uint8_t> dds(d + 1, d + n);

    Rs7Asset_ScaleDxt1(dds.empty() ? nullptr : dds.data(), dds.size(), (d[0] + 1) / 257.0f);

    return 0;
}

Seeds DdsSeeds() {
    std::vector<uint8_t> dds(1 + 128 + 2 * 2 * 8 + 8, 0);

    dds[0] = 128;
    std::memcpy(&dds[1], "DDS ", 4);
    const uint32_t size = 124, flags = 0x1007 | 0x20000, h = 8, w = 8, mips = 2, pf = 32, fourcc = 4;

    std::memcpy(&dds[1 + 4], &size, 4);
    std::memcpy(&dds[1 + 8], &flags, 4);
    std::memcpy(&dds[1 + 12], &h, 4);
    std::memcpy(&dds[1 + 16], &w, 4);
    std::memcpy(&dds[1 + 28], &mips, 4);
    std::memcpy(&dds[1 + 76], &pf, 4);
    std::memcpy(&dds[1 + 80], &fourcc, 4);
    std::memcpy(&dds[1 + 84], "DXT1", 4);

    for (size_t i = 1 + 128; i < dds.size(); ++i) {
        dds[i] = uint8_t(i * 37);
    }

    return {dds};
}

// [sprite lo][sprite hi][movie size, 4 bytes][movie][recipe]
int Recipe(const uint8_t* d, size_t n) {
    if (n < 6) {
        return 0;
    }

    uint32_t movieSize = 0;

    std::memcpy(&movieSize, d + 2, 4);
    if (movieSize > n - 6) {
        return 0;
    }

    const std::vector<uint8_t> movie(d + 6, d + 6 + movieSize), recipe(d + 6 + movieSize, d + n);
    std::vector<uint8_t> out;

    rs7diagram::SwapMovie(movie.empty() ? nullptr : movie.data(), movie.size(),
                          recipe.empty() ? nullptr : recipe.data(), recipe.size(), uint16_t(d[0] | d[1] << 8), &out);

    return 0;
}
}  // namespace rs7fuzz

namespace rs7fuzz {
namespace {
void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v.push_back(uint8_t(x >> (8 * i)));
    }
}
}  // namespace

// A synthetic movie (no game data): GFX header, a 1-bit RECT, DefineSprite 1, End; a recipe that copies the sprite.
Seeds RecipeSeeds() {
    std::vector<uint8_t> movie = {'G', 'F', 'X', 8, 22, 0, 0, 0, 0x08, 0, 0, 0, 1, 0};

    movie.push_back(uint8_t((59 << 6 | 4) & 0xFF));
    movie.push_back(uint8_t((59 << 6 | 4) >> 8));
    movie.insert(movie.end(), {1, 0, 0, 0, 0, 0});
    std::vector<uint8_t> recipe = {'R', '7', 'D', 'A', 1, 0, 0, 0};

    Put32(recipe, 6);
    Put32(recipe, 6);
    Put32(recipe, 1);
    recipe.resize(84, 0);
    recipe.push_back(0);
    Put32(recipe, 0);
    Put32(recipe, 6);
    std::vector<uint8_t> in = {1, 0};

    Put32(in, uint32_t(movie.size()));
    in.insert(in.end(), movie.begin(), movie.end());
    in.insert(in.end(), recipe.begin(), recipe.end());

    return {in};
}
}  // namespace rs7fuzz
