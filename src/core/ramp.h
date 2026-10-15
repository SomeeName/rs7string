#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Ramp model for string 6's own colour band (plan D3/D4/D5/D8). Pure: no D3D, no Windows.
// The stock colour ramps (note_gradient_map.dds / gradient_map_additive.dds) are 256x128, 16 rows
// of 8 lines; the game samples row i at V = (2i+1)/32. Our copy has the stock's exact size, format
// and level count; it repaints only lines that no stock V ever reads, at V6 below.

struct Rs7Image {
    unsigned w = 0, h = 0;
    std::vector<uint32_t> px;
};  // 0xAARRGGBB, row-major

struct Rs7Tap {
    unsigned line;
    unsigned w4;
};  // weight in quarters

// Taps of V = num/den on a level of height h, D3D9 texel centres (c = V*h - 0.5), clamp addressing.
// bilinear: the 1-2 lines with nonzero weight; point: floor(V*h). Exact rational arithmetic, lines
// ascending. The line set is always exact; w4 is exact whenever the weight is a multiple of 1/4
// (every stock V on h >= 8, V6 on h >= 32), otherwise rounded into 1..3 so a nonzero tap never
// disappears.
std::vector<Rs7Tap> Rs7Ramp_Taps(unsigned num, unsigned den, unsigned h, bool bilinear);

constexpr unsigned kRs7RampRows = 16;
// V6 per mode (copy layout, D3): mode 0 = 57/128 = 0.4453125, mode 1 = 121/128 = 0.9453125.
constexpr unsigned kRs7V6Num[2] = {57, 121};
constexpr unsigned kRs7V6Den = 128;
constexpr float kRs7V6[2] = {0.4453125f, 0.9453125f};
constexpr uint8_t kRs7Anchor[2] = {0x90, 0x4C};  // D8, HYPOTHESIS (RSMods anchor table)
constexpr unsigned kRs7Row6[2] = {7, 15};        // the row the custom band is recoloured from

// Lines of a level that any stock V (2i+1)/32, i = 0..15, reads with point or bilinear filtering.
std::vector<bool> Rs7Ramp_Protected(unsigned h);

struct Rs7RampBuild {
    std::vector<Rs7Image> levels;  // same sizes as the stock levels
    unsigned customLevels = 0;     // levels 0..customLevels-1 hold the pure custom colour at V6 (both modes)
};

// Copy of `stock` with only the V6 taps (both modes, point + bilinear) repainted, on every level where
// they avoid all protected lines; content = Rs7_Recolor of the stock row kRs7Row6[mode] at that level
// (its point line; the alpha byte is copied from that line). rgb[mode] = 0xRRGGBB.
// Stops at the first level where they collide (everything below that level stays a pure copy).
Rs7RampBuild Rs7Ramp_Build(const std::vector<Rs7Image>& stock, const uint32_t rgb[2]);

struct Rs7RampCheck {
    bool exact;
    bool v6Pure;
    unsigned changedLines;
    std::string why;
};

// exact: every protected line of every level is identical to stock and nothing else outside the V6 taps
// changed; v6Pure: customLevels >= 1 and on levels < customLevels every V6 tap holds the recoloured
// line. why: the first problem found (empty if none).
Rs7RampCheck Rs7Ramp_Verify(const std::vector<Rs7Image>& stock, const Rs7RampBuild& b, const uint32_t rgb[2]);

// D3DFORMAT / D3DPOOL / D3DUSAGE values used by the policy.
constexpr uint32_t kRs7FmtA8R8G8B8 = 21, kRs7FmtX8R8G8B8 = 22;
constexpr uint32_t kRs7FmtDxt1 = 0x31545844, kRs7FmtDxt5 = 0x35545844;  // MAKEFOURCC('D','X','T','1'/'5')
constexpr uint32_t kRs7PoolManaged = 1;

enum class Rs7RampVerdict { Swap, Refuse };

struct Rs7RampDesc {
    uint32_t format;
    unsigned w, h, levels;
    uint32_t pool;
    uint32_t usage;
};

struct Rs7RampPlan {
    Rs7RampVerdict verdict;
    bool exactPossible;
    uint32_t outFormat;
    const char* why;
};

// D3/D4/D5: 256x128 level 0, >= 1 level; A8R8G8B8 (21)/X8R8G8B8 (22) -> Swap (exact, same format);
// DXT1/DXT5 -> Swap only with allowInexact (out A8R8G8B8); pool != MANAGED (1) -> Refuse; any usage
// bit (RENDERTARGET, DYNAMIC, AUTOGENMIPMAP, ...) -> Refuse; anything else -> Refuse.
Rs7RampPlan Rs7Ramp_Plan(const Rs7RampDesc& d, bool allowInexact);
uint64_t Rs7Ramp_Hash(const std::vector<Rs7Image>& levels);  // FNV-1a 64 over sizes + texels

// nonUniformRows: rows (h/16 lines each) whose lines are not all identical; grey7/grey15: every texel
// of that row has R == G == B.
struct Rs7RowStats {
    unsigned nonUniformRows;
    bool grey7, grey15;
};

Rs7RowStats Rs7Ramp_RowStats(const Rs7Image& level0);  // for the log
