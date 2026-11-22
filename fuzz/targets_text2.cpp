// Fuzz targets: the byte-pattern parser and matcher (platform/pattern.h), the tuning-name composers
// (features/tuner/tuningname.h) and the log redactor (platform/log.h).
#include "features/tuner/tuningname.h"
#include "fuzz.h"
#include "platform/log.h"
#include "platform/pattern.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace rsxrfuzz {
namespace {
template <size_t N>
std::vector<uint8_t> Lit(const char (&s)[N]) {  // a literal's bytes, embedded NULs included
    return Bytes(std::string(s, N - 1));
}

// The NUL-separated fields of d[0, n): at most `max` of them, each cut at 200 bytes.
std::vector<std::string> Fields(const uint8_t* d, size_t n, size_t max) {
    std::vector<std::string> out(1);

    for (size_t i = 0; i < n; ++i) {
        if (d[i] == 0 && out.size() < max) {
            out.emplace_back();
        } else if (d[i] != 0 && out.back().size() < 200) {
            out.back().push_back(char(d[i]));
        }
    }

    return out;
}
}  // namespace

// [use histogram][max hits][pattern text] 0 [haystack...]
int Pattern(const uint8_t* d, size_t n) {
    if (n < 2) {
        return 0;
    }

    const std::vector<std::string> f = Fields(d + 2, n - 2, 2);
    const size_t skip = f[0].size() + 1;
    RsxrPattern p;

    if (!Rsxr_ParsePattern(f[0].c_str(), p) || skip >= n - 2) {
        return 0;
    }

    const uint8_t* hay = d + 2 + skip;
    const size_t size = n - 2 - skip;
    size_t hist[256];

    Rsxr_ByteHistogram(hay, size, hist);

    const std::vector<size_t> a = Rsxr_FindAll(hay, size, p, d[1], d[0] & 1 ? hist : nullptr);
    const std::vector<size_t> b = Rsxr_FindAll(hay, size, p, d[1]);

    if (a != b) {
        std::abort();  // the histogram only changes the speed
    }

    return 0;
}

Seeds PatternSeeds() {
    return {Lit("\x01\x04"
                "8B ?? 0C\0\x8B\x01\x0C\x8B\x02\x0C"),
            Lit("\x00\x02"
                "?? ??\0aaaa"),
            Lit("\x01\x01"
                "FF\0")};
}

// [mode] then NUL-separated fields: mode 0: 7 or 8 offsets (one byte each, as signed) for Name7 / Name8;
// mode 1: Compose(base, prefix, suffix) and Display(base); mode 2: MarkStock(stock, prefix, suffix).
int TuningName(const uint8_t* d, size_t n) {
    if (n < 1) {
        return 0;
    }

    if (d[0] % 3 == 0) {
        int t[8] = {};

        for (size_t i = 0; i < 8 && i + 1 < n; ++i) {
            t[i] = int8_t(d[1 + i]);
        }

        rsxrtn::Name7(t);
        rsxrtn::Name8(t);
        rsxrtn::ReferenceA(int8_t(d[n - 1]) * 8);
        rsxrtn::Decoration(int8_t(d[n - 1]) * 8);

        return 0;
    }

    std::vector<std::string> f = Fields(d + 1, n - 1, 3);

    f.resize(3);

    if (d[0] % 3 == 1) {
        rsxrtn::Compose(f[0].c_str(), f[1].c_str(), f[2].c_str());
        rsxrtn::Display(f[0]);
    } else {
        rsxrtn::MarkStock(f[0], f[1].c_str(), f[2].c_str());
    }

    return 0;
}

Seeds TuningNameSeeds() {
    return {{0, 0, 0, 0, 0, 0, 0, 0xFE, 0},
            {1, 'B', 'b', ' ', 'D', 'r', 'o', 'p', 0, 'p', 0, 's'},
            Lit("\x02"
                "E STANDARD: A436\0\0"
                "7Str")};
}

// Name lists then the message: profiles 0 users 0 computers 0 message (each list ';'-separated).
int Redact(const uint8_t* d, size_t n) {
    std::vector<std::string> f = Fields(d, n, 4);

    f.resize(4);

    RsxrLogRedactor r;
    std::vector<std::string>* dst[3] = {&r.profiles, &r.users, &r.computers};

    for (int k = 0; k < 3; ++k) {
        size_t at = 0;

        while (at <= f[k].size()) {
            const size_t semi = f[k].find(';', at);
            const size_t end = semi == std::string::npos ? f[k].size() : semi;

            dst[k]->push_back(f[k].substr(at, end - at));
            at = end + 1;
        }
    }

    RsxrLog_Redact(f[3], r);

    return 0;
}

Seeds RedactSeeds() {
    return {Lit("C:\\Users\\Bob\0Bob\0PC1\0open C:/users/bob/x.ini by Bob on PC1"), Lit("\0\0\0plain")};
}
}  // namespace rsxrfuzz
