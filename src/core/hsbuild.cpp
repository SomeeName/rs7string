#include "core/hsbuild.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace rs7hs {
namespace {
constexpr size_t kLimit = 64u << 20;
constexpr const char* kNames[] = {
    "rs7_inline7.nif",    "rs7_inline8.nif",          "rs7_peg.nif",    "rs7hs_env.dds",       "rs7hs_flat_n.dds",
    "rs7hs_hardware.dds", "rs7hs_hardware_gloss.dds", "rs7hs_wood.dds", "rs7hs_wood_gloss.dds"};
// The left-handed package (rs7string_headstock_lh.bin, templates epi_whilshire_cherry_lh.nif + the peg):
// only the mirrored model; the textures come from the right-handed package.
constexpr const char* kNamesLh[] = {"rs7_inline7_lh.nif"};

uint32_t Rot(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

std::array<uint8_t, 32> Digest(const Bytes& bytes) {
    // SHA-256, FIPS 180-4; arithmetic is unsigned modulo 2^32.
    constexpr uint32_t k[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint64_t bits = uint64_t(bytes.size()) * 8;
    const size_t padded = ((bytes.size() + 9 + 63) / 64) * 64;

    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[64]{};

        for (size_t i = 0; i < 64; ++i) {
            const size_t p = off + i;
            uint8_t b = 0;

            if (p < bytes.size()) {
                b = bytes[p];
            } else if (p == bytes.size()) {
                b = 0x80;
            } else if (p >= padded - 8) {
                b = uint8_t(bits >> ((padded - 1 - p) * 8));
            }
            w[i / 4] |= uint32_t(b) << ((3 - i % 4) * 8);
        }

        for (unsigned i = 16; i < 64; ++i) {
            const auto x = w[i - 15], y = w[i - 2];

            w[i] = w[i - 16] + (Rot(x, 7) ^ Rot(x, 18) ^ (x >> 3)) + w[i - 7] + (Rot(y, 17) ^ Rot(y, 19) ^ (y >> 10));
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], v = h[7];

        for (unsigned i = 0; i < 64; ++i) {
            const auto t1 = v + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto t2 = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

            v = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += v;
    }

    std::array<uint8_t, 32> out{};

    for (unsigned i = 0; i < 32; ++i) {
        out[i] = uint8_t(h[i / 4] >> ((3 - i % 4) * 8));
    }

    return out;
}

std::string Hex(const uint8_t* p, size_t n) {
    std::string out;

    out.reserve(n * 2);

    for (size_t i = 0; i < n; ++i) {
        out += "0123456789abcdef"[p[i] >> 4];
        out += "0123456789abcdef"[p[i] & 15];
    }

    return out;
}

[[noreturn]] void Bad(const char* error) {
    throw std::runtime_error(error);
}

struct Reader {
    Bytes data;
    size_t pos = 0;

    explicit Reader(const Bytes& package) {
        if (package.size() < 6 || package.size() > kLimit || std::memcmp(package.data(), "RS7H1\n", 6)) {
            Bad("package magic/size");
        }
        if ((package.size() - 6) % 2) {
            Bad("package hex length");
        }
        data.reserve((package.size() - 6) / 2);
        auto nibble = [](uint8_t c) -> uint8_t {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }

            Bad("package hex digit");
        };

        for (size_t i = 6; i < package.size(); i += 2) {
            data.push_back(uint8_t(nibble(package[i]) * 16 + nibble(package[i + 1])));
        }
    }

    const uint8_t* Take(size_t n) {
        if (n > data.size() - pos) {
            Bad("truncated package");
        }
        const auto p = data.data() + pos;

        pos += n;

        return p;
    }

    uint32_t U32() {
        const auto p = Take(4);

        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    void Header(const Bytes* templates) {
        if (U32() != 1) {
            Bad("package version");
        }

        for (unsigned i = 0; i < 2; ++i) {
            const auto want = Take(32);

            if (templates) {
                if (templates[i].empty() || templates[i].size() > kLimit) {
                    Bad("template size");
                }
                const auto have = Digest(templates[i]);

                if (std::memcmp(want, have.data(), 32)) {
                    Bad("template hash mismatch");
                }
            }
        }
    }
};

void Parse(const Bytes& package, const Bytes* templates, std::vector<File>& files, std::vector<Expected>& manifest) {
    Reader r(package);

    r.Header(templates);
    const auto count = r.U32();
    const char* const* names = count == std::size(kNames) ? kNames : count == std::size(kNamesLh) ? kNamesLh : nullptr;

    if (!names) {
        Bad("artifact count");
    }
    size_t total = 0;

    for (unsigned i = 0; i < count; ++i) {
        const auto namelen = r.U32();

        if (namelen > 64) {
            Bad("artifact name length");
        }
        const auto np = r.Take(namelen);
        const std::string name(reinterpret_cast<const char*>(np), namelen);

        if (name != names[i]) {
            Bad("artifact name/order");
        }
        const auto size = r.U32();

        if (!size || size > kLimit - total) {
            Bad("artifact size");
        }
        total += size;
        const auto hash = r.Take(32);
        const std::string expected = Hex(hash, 32);
        const auto nops = r.U32();

        if (!nops || nops > size || nops > (r.data.size() - r.pos) / 5) {
            Bad("instruction count");
        }
        Bytes out;

        if (templates) {
            out.reserve(size);
        }
        size_t produced = 0;

        for (unsigned op = 0; op < nops; ++op) {
            const auto tag = *r.Take(1);
            const uint8_t* from = nullptr;
            uint32_t n = 0;

            if (tag == 0) {
                n = r.U32();
                from = r.Take(n);
            } else if (tag == 1) {
                const auto tid = r.U32(), offset = r.U32();

                n = r.U32();
                if (tid > 1 || offset > kLimit || n > kLimit - offset) {
                    Bad("copy range");
                }

                if (templates) {
                    if (offset > templates[tid].size() || n > templates[tid].size() - offset) {
                        Bad("copy range");
                    }
                    from = templates[tid].data() + offset;
                }
            } else {
                Bad("instruction tag");
            }

            if (!n || n > size - produced) {
                Bad("instruction output size");
            }
            produced += n;
            if (templates) {
                out.insert(out.end(), from, from + n);
            }
        }

        if (produced != size) {
            Bad("output size mismatch");
        }
        if (templates && Hash(out) != expected) {
            Bad("output hash mismatch");
        }
        manifest.push_back({name, size, expected});
        if (templates) {
            files.push_back({name, std::move(out)});
        }
    }

    if (r.pos != r.data.size()) {
        Bad("trailing package bytes");
    }
}
}  // namespace

std::string Hash(const Bytes& bytes) {
    const auto d = Digest(bytes);

    return Hex(d.data(), d.size());
}

bool Build(const Bytes& guitar, const Bytes& peg, const Bytes& package, std::vector<File>& outputs,
           std::string& error) {
    outputs.clear();
    error.clear();

    try {
        const Bytes templates[] = {guitar, peg};
        std::vector<Expected> manifest;
        std::vector<File> candidate;

        Parse(package, templates, candidate, manifest);
        outputs = std::move(candidate);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool Manifest(const Bytes& package, std::vector<Expected>& outputs, std::string& error) {
    outputs.clear();
    error.clear();

    try {
        std::vector<File> files;
        std::vector<Expected> candidate;

        Parse(package, nullptr, files, candidate);
        outputs = std::move(candidate);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool CheckTemplates(const Bytes& guitar, const Bytes& peg, const Bytes& package, std::string& error) {
    error.clear();

    try {
        Reader r(package);
        const Bytes templates[] = {guitar, peg};

        r.Header(templates);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
}  // namespace rs7hs
