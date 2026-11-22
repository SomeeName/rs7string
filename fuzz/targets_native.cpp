// Fuzz targets: the converters of the native format (model/nativesng.h, model/nativemanifest.h,
// docs/NATIVE-FORMAT.md). Both read file data of a package.
#include "fuzz.h"
#include "model/nativemanifest.h"
#include "model/nativesng.h"
#include <cstdlib>
#include <cstring>

namespace rsxrfuzz {
namespace {
// An SNXR body with no beats, notes or chords: the header, fourteen empty sections, MetaData with the tuning, and
// the padding a writer appends.
std::vector<uint8_t> EmptyChart(uint8_t instrument, uint8_t n) {
    std::vector<uint8_t> b = {'S', 'N', 'X', 'R', 1, instrument, n, 0};

    b.resize(b.size() + 14 * 4 + 79);  // the section counts, MetaData up to its tuning count
    b.push_back(n);
    b.resize(b.size() + 3 + 2 * n + 12 + 256);

    return b;
}
}  // namespace

// The inflated chart body, as the load hook hands it over.
int NativeSng(const uint8_t* d, size_t n) {
    std::vector<uint8_t> out;
    char why[200];

    if (rsxrnative::Convert(d, n, &out, why, sizeof why) == rsxrnative::Status::Ok && out.size() != n) {
        std::abort();  // the result goes back into the game's buffer of n bytes
    }

    return 0;
}

Seeds NativeSngSeeds() {
    return {EmptyChart(0, 7), EmptyChart(0, 8), EmptyChart(0, 6), EmptyChart(1, 4), EmptyChart(1, 5), EmptyChart(1, 6)};
}

// The manifest text at the game's JSON parser: its extent, then the rewrite of exactly that much.
int NativeManifest(const uint8_t* d, size_t n) {
    const char* text = reinterpret_cast<const char*>(d);
    const size_t extent = rsxrnative::JsonExtent(text, n);

    if (extent > n) {
        std::abort();
    }

    std::string out;
    rsxrnative::ManifestStats stats;

    // A rewritten text is one whole JSON value again.
    if (extent && rsxrnative::RewriteManifest(text, extent, &out, &stats) &&
        rsxrnative::JsonExtent(out.c_str(), out.size()) != out.size()) {
        std::abort();
    }

    return 0;
}

Seeds NativeManifestSeeds() {
    return {Bytes("{\"Entries\": {\"A\": {\"Attributes\": {\"ArrangementName\": \"Lead\", \"ExtendedRange\": "
                  "{\"Version\": 1}, \"Tuning\": {\"string0\": -2, \"string1\": 0, "
                  "\"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0, \"string6\": 0}}}}}"),
            Bytes("\xEF\xBB\xBF{\"ArrangementName\": \"Bass\", \"ExtendedRange\": {\"Version\": 1},\r\n \"Tuning\": "
                  "{\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, "
                  "\"string5\": 24}, \"x\": [1, \"\\\"}\", {}]}trailing"),
            Bytes("{\"ExtendedRange\": 1, \"Tuning\": {}, \"Tuning\": {\"string0\": {\"ExtendedRange\": {}}}}")};
}
}  // namespace rsxrfuzz
