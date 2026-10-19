#pragma once
#include "features/colours/colours.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace rs7fix {
// Worst-case synthetic stock: every line different (not only every row), all levels, 1..9 levels.
inline std::vector<Rs7Image> Stock(unsigned levels) {
    std::vector<Rs7Image> v;

    for (unsigned k = 0; k < levels; ++k) {
        Rs7Image im;

        im.w = std::max(1u, 256u >> k);
        im.h = std::max(1u, 128u >> k);
        im.px.resize(size_t(im.w) * im.h);

        for (unsigned y = 0; y < im.h; ++y) {
            for (unsigned x = 0; x < im.w; ++x) {
                im.px[y * im.w + x] = (k << 24) | (y << 12) | x;
            }
        }

        v.push_back(im);
    }

    return v;
}

// Like the game's ramps: 16 rows, every line of a row identical; rows 7 and 15 grey (R = G = B).
// Smaller levels: the row of each line (h/16 lines per row while h >= 16, else line i = row 16i/h).
inline std::vector<Rs7Image> UniformStock(unsigned levels) {
    std::vector<Rs7Image> v;

    for (unsigned k = 0; k < levels; ++k) {
        Rs7Image im;

        im.w = std::max(1u, 256u >> k);
        im.h = std::max(1u, 128u >> k);
        im.px.resize(size_t(im.w) * im.h);

        for (unsigned y = 0; y < im.h; ++y) {
            const unsigned row = y * 16 / im.h;

            for (unsigned x = 0; x < im.w; ++x) {
                const uint32_t g = (x * 255) / std::max(1u, im.w - 1);
                const uint32_t px =
                    (row == 7 || row == 15) ? (g << 16) | (g << 8) | g : ((row * 16) << 16) | (g << 8) | (255 - g);

                im.px[y * im.w + x] = px;
            }
        }

        v.push_back(im);
    }

    return v;
}
}  // namespace rs7fix
