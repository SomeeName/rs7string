// The ramp copy model: which texture lines the game reads, the copy with string 6's band, its check and policy.
#include "features/colours/colours.h"
#include <cstdio>
#include <utility>

namespace {
int64_t FloorDiv(int64_t a, int64_t b) {  // b > 0
    const int64_t q = a / b;

    return (a % b != 0 && a < 0) ? q - 1 : q;
}

unsigned Clamp(int64_t line, unsigned h) {
    if (line < 0) {
        return 0;
    }
    if (line >= int64_t(h)) {
        return h - 1;
    }

    return unsigned(line);
}

void AddTap(std::vector<Rs7Tap>& v, unsigned line, unsigned w4) {
    for (auto& t : v) {
        if (t.line == line) {
            t.w4 += w4;
            return;
        }
    }

    v.push_back({line, w4});
    if (v.size() == 2 && v[0].line > v[1].line) {
        std::swap(v[0], v[1]);
    }
}

uint32_t RecolorPixel(uint32_t src, unsigned mode, uint32_t rgb) {
    uint32_t out = src & 0xFF000000u;

    for (int sh = 16; sh >= 0; sh -= 8) {
        const uint8_t g = uint8_t(src >> sh), c = uint8_t(rgb >> sh);

        out |= uint32_t(Rs7_Recolor(g, kRs7Anchor[mode], c)) << sh;
    }

    return out;
}

// Stock line the band of `mode` is recoloured from: the point tap of row kRs7Row6[mode].
unsigned SourceLine(unsigned mode, unsigned h) {
    return Rs7Ramp_Taps(2 * kRs7Row6[mode] + 1, 2 * kRs7RampRows, h, false)[0].line;
}

// Stamps band `band` (0 = V6, 1 = V7) into stamp as 1 + 2 * band + mode; false (stamp unchanged) if one of its taps is
// protected or already stamped by another band.
bool StampBand(unsigned h, const std::vector<bool>& prot, unsigned band, std::vector<uint8_t>& stamp) {
    std::vector<uint8_t> next = stamp;
    const unsigned* num = band ? kRs7V7Num : kRs7V6Num;

    for (unsigned m = 0; m < 2; ++m) {
        const uint8_t mark = uint8_t(1 + 2 * band + m);

        for (bool bil : {true, false}) {
            for (const auto& t : Rs7Ramp_Taps(num[m], kRs7BandDen, h, bil)) {
                if (prot[t.line] || (next[t.line] && next[t.line] != mark)) {
                    return false;
                }

                next[t.line] = mark;
            }
        }
    }

    stamp.swap(next);

    return true;
}

// stamp[y] = 0 (not stamped), 1 + mode (V6) or 3 + mode (V7). False if the V6 taps do not fit (the level gets no
// custom band); with `eight` the V7 taps are added only where they fit too (a smaller level may lack them).
bool BandLines(unsigned h, const std::vector<bool>& prot, bool eight, std::vector<uint8_t>& stamp) {
    stamp.assign(h, 0);
    if (!StampBand(h, prot, 0, stamp)) {
        return false;
    }

    if (eight) {
        StampBand(h, prot, 1, stamp);
    }

    return true;
}

// The mode of a stamped line (stamp = 1 + 2 * band + mode, StampBand).
unsigned StampMode(uint8_t stamp) {
    return (stamp - 1u) & 1u;
}

// The band colour of a stamped line (BandLines).
uint32_t StampRgb(uint8_t stamp, const uint32_t rgb[2], const uint32_t* rgb8) {
    const unsigned m = StampMode(stamp);

    return stamp > 2 ? rgb8[m] : rgb[m];
}

bool SameSize(const Rs7Image& a, const Rs7Image& b) {
    return a.w == b.w && a.h == b.h && a.px.size() == size_t(a.w) * a.h && b.px.size() == a.px.size();
}

std::string Fmt(const char* f, unsigned a, unsigned b) {
    char buf[96];

    std::snprintf(buf, sizeof buf, f, a, b);

    return buf;
}

struct LineCheck {
    bool differs;  // line y of the copy differs from stock
    bool pure;     // stamp != 0: it holds the recoloured source line of mode stamp - 1
};

LineCheck CheckLine(const Rs7Image& s, const Rs7Image& d, unsigned y, uint8_t stamp, const unsigned src[2],
                    const uint32_t rgb[2], const uint32_t* rgb8) {
    const uint32_t* a = &s.px[size_t(y) * s.w];
    const uint32_t* c = &d.px[size_t(y) * s.w];
    LineCheck r{false, true};

    for (unsigned x = 0; x < s.w; ++x) {
        if (a[x] != c[x]) {
            r.differs = true;
        }

        if (stamp) {
            const unsigned m = StampMode(stamp);

            if (c[x] != RecolorPixel(s.px[size_t(src[m]) * s.w + x], m, StampRgb(stamp, rgb, rgb8))) {
                r.pure = false;
            }
        }
    }

    return r;
}
}  // namespace

std::vector<Rs7Tap> Rs7Ramp_Taps(unsigned num, unsigned den, unsigned h, bool bilinear) {
    std::vector<Rs7Tap> v;
    if (h == 0 || den == 0) {
        return v;
    }

    if (!bilinear) {
        v.push_back({Clamp(FloorDiv(int64_t(num) * h, den), h), 4});
        return v;
    }

    // c = (2*num*h - den) / (2*den) = n + fnum/den2
    const int64_t den2 = 2 * int64_t(den);
    const int64_t cnum = 2 * int64_t(num) * h - den;
    const int64_t n = FloorDiv(cnum, den2);
    const int64_t fnum = cnum - n * den2;  // [0, den2)

    if (fnum == 0) {
        v.push_back({Clamp(n, h), 4});
        return v;
    }

    unsigned hi;  // weight of line n+1 in quarters

    if ((4 * fnum) % den2 == 0) {
        hi = unsigned(4 * fnum / den2);
    } else {
        hi = unsigned((8 * fnum + den2) / (2 * den2));  // round to nearest quarter
        if (hi < 1) {
            hi = 1;
        }
        if (hi > 3) {
            hi = 3;
        }
    }

    AddTap(v, Clamp(n, h), 4 - hi);
    AddTap(v, Clamp(n + 1, h), hi);

    return v;
}

std::vector<bool> Rs7Ramp_Protected(unsigned h) {
    std::vector<bool> p(h, false);

    for (unsigned i = 0; i < kRs7RampRows; ++i) {
        for (bool bil : {true, false}) {
            for (const auto& t : Rs7Ramp_Taps(2 * i + 1, 2 * kRs7RampRows, h, bil)) {
                p[t.line] = true;
            }
        }
    }

    return p;
}

Rs7RampBuild Rs7Ramp_Build(const std::vector<Rs7Image>& stock, const uint32_t rgb[2], const uint32_t* rgb8) {
    Rs7RampBuild b;

    b.levels = stock;

    for (size_t k = 0; k < stock.size(); ++k) {
        const Rs7Image& s = stock[k];
        if (s.w == 0 || s.h == 0 || s.px.size() != size_t(s.w) * s.h) {
            break;
        }

        std::vector<uint8_t> stamp;
        if (!BandLines(s.h, Rs7Ramp_Protected(s.h), rgb8 != nullptr, stamp)) {
            break;
        }

        const unsigned src[2] = {SourceLine(0, s.h), SourceLine(1, s.h)};
        Rs7Image& d = b.levels[k];

        for (unsigned y = 0; y < s.h; ++y) {
            if (!stamp[y]) {
                continue;
            }

            const unsigned m = StampMode(stamp[y]);
            const uint32_t colour = StampRgb(stamp[y], rgb, rgb8);

            for (unsigned x = 0; x < s.w; ++x) {
                d.px[size_t(y) * s.w + x] = RecolorPixel(s.px[size_t(src[m]) * s.w + x], m, colour);
            }
        }

        ++b.customLevels;
    }

    return b;
}

Rs7RampCheck Rs7Ramp_Verify(const std::vector<Rs7Image>& stock, const Rs7RampBuild& b, const uint32_t rgb[2],
                            const uint32_t* rgb8) {
    Rs7RampCheck r{true, true, 0, std::string()};
    auto fail = [&r](bool& flag, std::string why) {
        flag = false;
        if (r.why.empty()) {
            r.why = std::move(why);
        }
    };

    if (b.levels.size() != stock.size() || b.customLevels > stock.size()) {
        fail(r.exact, "level count differs");
        r.v6Pure = false;
        return r;
    }

    if (b.customLevels == 0) {
        fail(r.v6Pure, "no level holds the custom band");
    }

    for (size_t k = 0; k < stock.size(); ++k) {
        const Rs7Image& s = stock[k];
        const Rs7Image& d = b.levels[k];

        if (!SameSize(s, d)) {
            fail(r.exact, Fmt("level %u: size differs (%u)", unsigned(k), d.h));
            if (k < b.customLevels) {
                r.v6Pure = false;
            }
            continue;
        }

        const auto prot = Rs7Ramp_Protected(s.h);
        std::vector<uint8_t> stamp(s.h, 0);

        if (k < b.customLevels && !BandLines(s.h, prot, rgb8 != nullptr, stamp)) {
            fail(r.exact, Fmt("level %u: V6 taps collide with stock taps (%u)", unsigned(k), s.h));
            r.v6Pure = false;
            stamp.assign(s.h, 0);
        }

        const unsigned src[2] = {SourceLine(0, s.h), SourceLine(1, s.h)};

        for (unsigned y = 0; y < s.h; ++y) {
            const LineCheck line = CheckLine(s, d, y, stamp[y], src, rgb, rgb8);
            const bool differs = line.differs, pure = line.pure;

            if (differs) {
                ++r.changedLines;
            }

            if (stamp[y]) {
                if (!pure) {
                    fail(r.v6Pure, Fmt("level %u line %u: custom band differs", unsigned(k), y));
                }
            } else if (differs) {
                fail(r.exact, Fmt(prot[y] ? "level %u line %u: protected line changed"
                                          : "level %u line %u: line outside the V6 taps changed",
                                  unsigned(k), y));
            }
        }
    }

    return r;
}

Rs7RampPlan Rs7Ramp_Plan(const Rs7RampDesc& d) {
    if (d.w != 256 || d.h != 128) {
        return {Rs7RampVerdict::Refuse, "level 0 is not 256x128"};
    }
    if (d.levels == 0) {
        return {Rs7RampVerdict::Refuse, "no levels"};
    }
    if (d.pool != kRs7PoolManaged) {
        return {Rs7RampVerdict::Refuse, "pool is not MANAGED"};
    }
    if (d.usage != 0) {
        return {Rs7RampVerdict::Refuse, "usage flags set"};
    }
    if (d.format == kRs7FmtA8R8G8B8 || d.format == kRs7FmtX8R8G8B8) {
        return {Rs7RampVerdict::Swap, "exact copy, same format"};
    }

    if (d.format == kRs7FmtDxt1 || d.format == kRs7FmtDxt5) {
        return {Rs7RampVerdict::Refuse, "DXT ramps are not copied"};
    }

    return {Rs7RampVerdict::Refuse, "unsupported format"};
}

uint64_t Rs7Ramp_Hash(const std::vector<Rs7Image>& levels) {
    uint64_t h = 0xCBF29CE484222325ull;
    auto feed = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= uint8_t(v >> (8 * i));
            h *= 0x100000001B3ull;
        }
    };

    for (const auto& im : levels) {
        feed(im.w);
        feed(im.h);

        for (uint32_t p : im.px) {
            feed(p);
        }
    }

    return h;
}

Rs7RowStats Rs7Ramp_RowStats(const Rs7Image& im) {
    Rs7RowStats s{0, false, false};
    if (im.w == 0 || im.h == 0 || im.px.size() != size_t(im.w) * im.h) {
        return s;
    }

    auto grey = [](uint32_t p) {
        return ((p >> 16) & 0xFF) == ((p >> 8) & 0xFF) && ((p >> 8) & 0xFF) == (p & 0xFF);
    };

    for (unsigned r = 0; r < kRs7RampRows; ++r) {
        const unsigned y0 = r * im.h / kRs7RampRows, y1 = (r + 1) * im.h / kRs7RampRows;
        bool uniform = true, allGrey = y1 > y0;

        for (unsigned y = y0; y < y1; ++y) {
            for (unsigned x = 0; x < im.w; ++x) {
                const uint32_t p = im.px[size_t(y) * im.w + x];

                if (p != im.px[size_t(y0) * im.w + x]) {
                    uniform = false;
                }
                if (!grey(p)) {
                    allGrey = false;
                }
            }
        }

        if (!uniform) {
            ++s.nonUniformRows;
        }
        if (r == 7) {
            s.grey7 = allGrey;
        }
        if (r == 15) {
            s.grey15 = allGrey;
        }
    }

    return s;
}
