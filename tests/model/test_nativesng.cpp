#include "model/extblock.h"
#include "model/nativesng.h"
#include "test.h"
#include <algorithm>
#include <cstring>

namespace {
using Bytes = std::vector<uint8_t>;
using rsxrnative::Status;

template <class T>
void Put(Bytes& b, T v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);

    b.insert(b.end(), p, p + sizeof v);
}

template <class T>
void Set(Bytes& b, size_t at, T v) {
    std::memcpy(b.data() + at, &v, sizeof v);
}

template <class T>
T Get(const Bytes& b, size_t at) {
    T v;

    std::memcpy(&v, b.data() + at, sizeof v);

    return v;
}

// Hand-specified low-to-high native order for each supported instrument. Never calls converter helpers.
const int kGuitarSlots[] = {7, 6, 0, 1, 2, 3, 4, 5};
const int kBassSlots[] = {4, 0, 1, 2, 3, 5};

struct Song {
    Bytes native, stock;
    std::vector<size_t> counts, bendCounts;
    int instrument, n;
    size_t templates = 0, techniques = 0, tuning = 0, end = 0;

    int Slot(int i) const { return instrument ? kBassSlots[i + (n == 4 ? 1 : 0)] : kGuitarSlots[i + 8 - n]; }

    int Native(int slot) const {
        for (int i = 0; i < n; ++i) {
            if (Slot(i) == slot) {
                return i;
            }
        }

        return -1;
    }

    void Raw(size_t bytes, unsigned seed) {
        for (size_t i = 0; i < bytes; ++i) {
            const auto v = static_cast<uint8_t>((i + seed) % 251);

            native.push_back(v);
            stock.push_back(v);
        }
    }

    void Count(uint32_t count) {
        counts.push_back(native.size());
        Put(native, count);
        Put(stock, count);
    }

    void Fixed(uint32_t count, size_t stride, unsigned seed) {
        Count(count);
        Raw(count * stride, seed);
    }

    static void Template(Bytes& b, int row, int columns, const Song* song) {
        Put<uint32_t>(b, 0x12340000u + row);

        for (int field = 0; field < 3; ++field) {
            for (int col = 0; col < columns; ++col) {
                const int i = song ? song->Native(col) : col;

                if (field == 2) {
                    Put<int32_t>(b, i < 0 ? -1 : 100 + row * 20 + i);
                } else {
                    Put<int8_t>(b, static_cast<int8_t>(i < 0 ? -1 : field ? (row + i) % 5 : 1 + row + i));
                }
            }
        }

        for (int i = 0; i < 32; ++i) {
            b.push_back(static_cast<uint8_t>(i + row));
        }
    }

    void Technique(Bytes& b, int row, int columns, bool mapped) {
        for (int field = 0; field < 5; ++field) {
            for (int col = 0; col < columns; ++col) {
                const int i = mapped ? Native(col) : col;

                switch (field) {
                    case 0:
                        Put<uint32_t>(b, i < 0 ? 0 : 0x2000 + row * 0x100 + i);
                        break;
                    case 1:
                        for (int bend = 0; bend < 32; ++bend) {
                            Put<float>(b, i < 0 ? 0.0f : row * 10.0f + i + bend * 0.125f);
                            Put<float>(b, i < 0 ? 0.0f : 0.5f + bend);
                            Put<uint32_t>(b, i < 0 ? 0 : 0xA000 + bend);
                        }

                        if (!mapped) {
                            bendCounts.push_back(b.size());
                        }
                        Put<uint32_t>(b, i < 0 ? 0 : row ? 32 : 2);
                        break;
                    case 2:
                    case 3:
                        Put<int8_t>(b, static_cast<int8_t>(i < 0 ? -1 : field * 2 - 2 + row + i));
                        break;
                    case 4:
                        Put<int16_t>(b, static_cast<int16_t>(i < 0 ? 0 : 20 + row + i));
                        break;
                }
            }
        }
    }

    void Sections() {
        Count(2);

        for (int row = 0; row < 2; ++row) {
            Raw(52, 11 + row);

            for (int i = 0; i < 36; ++i) {
                native.push_back(static_cast<uint8_t>((1 << (i % n)) | (1 << ((i + 1) % n))));
                stock.push_back(static_cast<uint8_t>((1 << Slot(i % n)) | (1 << Slot((i + 1) % n))));
            }
        }
    }

    void Levels() {
        Count(2);

        for (int level = 0; level < 2; ++level) {
            Raw(4, level);
            Fixed(2, 28, 17);
            Fixed(1, 12, 18);
            Fixed(1, 20, 19);
            Fixed(2, 20, 20);
            Count(n + 1);

            for (int i = 0; i <= n; ++i) {
                const size_t a = native.size(), b = stock.size();

                Raw(63, 21 + i);
                native[a + 16] = static_cast<uint8_t>(i == n ? 255 : i);
                stock[b + 16] = static_cast<uint8_t>(i == n ? 255 : Slot(i));
                Fixed(i % 3, 12, 22);
            }

            Fixed(2, 4, 23);
            Fixed(3, 4, 24);
            Fixed(1, 4, 25);
        }
    }

    Song(int inst, int strings, bool vocals = true, size_t padding = 4096) : instrument(inst), n(strings) {
        native = {'S', 'N', 'X', 'R', 1, static_cast<uint8_t>(inst), static_cast<uint8_t>(n), 0};

        Fixed(2, 16, 1);
        Fixed(1, 44, 2);
        Count(2);
        templates = native.size();

        for (int row = 0; row < 2; ++row) {
            Template(native, row, n, nullptr);
            Template(stock, row, 6, this);
        }

        Count(2);
        techniques = native.size();

        for (int row = 0; row < 2; ++row) {
            Technique(native, row, n, false);
            Technique(stock, row, 6, true);
        }

        Fixed(vocals ? 1 : 0, 60, 3);

        if (vocals) {
            Fixed(2, 32, 4);
            Fixed(1, 144, 5);
            Fixed(2, 44, 6);
        }

        Fixed(2, 24, 7);
        Fixed(1, 16, 8);
        Count(2);
        Raw(4, 9);
        Fixed(3, 4, 10);
        Raw(4, 11);
        Fixed(1, 4, 12);
        Fixed(1, 260, 13);
        Fixed(2, 260, 14);
        Fixed(2, 8, 15);
        Fixed(1, 8, 16);
        Sections();
        Levels();
        Raw(79, 26);
        tuning = native.size();
        counts.push_back(tuning);
        Put<uint32_t>(native, n);
        Put<uint32_t>(stock, inst ? 6 : n);

        for (int i = 0; i < n; ++i) {
            Put<int16_t>(native, static_cast<int16_t>(-7 - i));
        }

        for (int slot = 0; slot < (inst ? 6 : n); ++slot) {
            const int i = Native(slot);

            Put<int16_t>(stock, static_cast<int16_t>(i < 0 ? 0 : -7 - i));
        }

        Raw(12, 27);
        end = native.size();
        native.resize(end + padding, 0);
    }
};

Status Convert(const Bytes& input, Bytes* output, char* why, size_t cap) {
    return rsxrnative::Convert(input.data(), input.size(), output, why, cap);
}

void CheckBlock(const Song& song, const Bytes& out) {
    const size_t tail = song.stock.size();
    const bool extended = song.n > (song.instrument ? 4 : 6);

    if (!extended) {
        CHECK(std::all_of(out.begin() + tail, out.end(), [](uint8_t b) { return b == 0; }));
        return;
    }

    rsxrext::Block block;
    char why[180];

    CHECK(rsxrext::Parse(out.data() + tail, out.size() - tail, 2, 2, song.instrument ? 6 : song.n, &block, why,
                         sizeof why) == rsxrext::Status::Ok);
    CHECK(block.instrument == song.instrument && block.stringCount == song.n && block.skipped == 1);
    CHECK(block.Columns() == (song.instrument ? 0 : song.n - 6));
    CHECK(block.hasTemplates == !song.instrument && block.hasChordNotes == !song.instrument);

    for (int row = 0; row < 2 && !song.instrument; ++row) {
        for (int slot = 6; slot < song.n; ++slot) {
            const int i = song.Native(slot), col = slot - 6;
            const size_t at = row * block.Columns() + col;

            CHECK(block.strings[col] == slot);
            CHECK(block.fret[at] == 1 + row + i && block.finger[at] == (row + i) % 5);
            const auto& tech = block.tech[at];

            CHECK(tech.mask == static_cast<uint32_t>(0x2000 + row * 0x100 + i));
            CHECK(tech.slideTo == 2 + row + i && tech.slideUnpitchTo == 4 + row + i);
            CHECK(tech.vibrato == 20 + row + i && tech.bends.size() == (row ? 32u : 2u));

            for (size_t bend = 0; bend < tech.bends.size(); ++bend) {
                CHECK(tech.bends[bend].time == row * 10.0f + i + bend * 0.125f);
                CHECK(tech.bends[bend].step == 0.5f + bend);
                CHECK(tech.bends[bend].reserved == 0xA000 + bend);
            }
        }
    }

    size_t at = tail + 16;
    const uint32_t sections = Get<uint32_t>(out, tail + 12);

    for (uint32_t s = 0; s < sections; ++s) {
        const uint32_t bytes = Get<uint32_t>(out, at + 8);

        if (s + 1 == sections) {
            CHECK(std::memcmp(out.data() + at, "PAD0", 4) == 0);
            CHECK(Get<uint16_t>(out, at + 6) == 0);
            CHECK(at + 12 + bytes == out.size());
            CHECK(std::all_of(out.begin() + at + 12, out.end(), [](uint8_t b) { return b == 0; }));
        }

        at += 12 + bytes;
    }
}

void CheckSong(int instrument, int n, bool vocals) {
    const Song song(instrument, n, vocals);
    Bytes out{9, 8, 7};
    char why[180] = "old reason";
    const auto status = Convert(song.native, &out, why, sizeof why);

    CHECK(status == Status::Ok);
    if (status != Status::Ok) {
        return;
    }

    CHECK(out.size() == song.native.size() && why[0] == 0);
    // Includes every template/technique column, unused stock columns, each note in both levels,
    // 36 masks per section, tuning entries, and every unchanged byte in every stock section.
    CHECK(std::equal(song.stock.begin(), song.stock.end(), out.begin()));
    CheckBlock(song, out);
}
}  // namespace

TEST(nativesng_guitar_columns_and_tail) {
    CheckSong(0, 6, true);
    CheckSong(0, 7, true);
    CheckSong(0, 8, true);
}

TEST(nativesng_bass_columns_and_tail) {
    CheckSong(1, 4, true);
    CheckSong(1, 5, true);
    CheckSong(1, 6, true);
}

TEST(nativesng_without_vocals) {
    CheckSong(0, 8, false);
    CheckSong(1, 4, false);
}

TEST(nativesng_detection_and_bad_headers) {
    Song song(0, 7);

    CHECK(rsxrnative::IsNative(song.native.data(), song.native.size()));
    CHECK(!rsxrnative::IsNative(nullptr, 4));

    for (size_t size = 0; size < 4; ++size) {
        CHECK(!rsxrnative::IsNative(song.native.data(), size));
    }

    const size_t offsets[] = {4, 5, 6, 7};
    const uint8_t values[] = {2, 2, 17, 1};
    Bytes out{1, 2, 3};
    char why[180];

    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        auto b = song.native;

        b[offsets[i]] = values[i];
        CHECK(Convert(b, &out, why, sizeof why) == Status::Bad);
        CHECK(std::strstr(why, "byte") != nullptr && why[0] != 0);
        CHECK(out == Bytes({1, 2, 3}));
    }

    song.native[0] = 0;
    CHECK(Convert(song.native, &out, why, sizeof why) == Status::NotNative);
}

TEST(nativesng_rejects_counts_and_nonzero_padding) {
    const Song song(0, 8);
    Bytes out;
    char why[180];

    for (size_t offset : song.counts) {
        auto b = song.native;

        Set<uint32_t>(b, offset, UINT32_MAX);
        CHECK(Convert(b, &out, why, sizeof why) == Status::Bad);
        CHECK(why[0] != 0);
    }

    for (size_t offset : song.bendCounts) {
        auto b = song.native;

        Set<uint32_t>(b, offset, 33);
        CHECK(Convert(b, &out, why, sizeof why) == Status::Bad);
    }

    auto b = song.native;

    b.back() = 1;
    CHECK(Convert(b, &out, why, sizeof why) == Status::Bad);
    Set<uint32_t>(b, song.tuning, 7);
    CHECK(Convert(b, &out, why, sizeof why) == Status::Bad);
}

TEST(nativesng_padding_missing_bytes_and_exact_fit) {
    // Bass-4: 2 templates expand by 24, 2 ChordNotes by 1584, tuning by 4, header removes 8.
    Song song(1, 4, false, 0);
    Bytes out{7};
    char why[180];

    CHECK(Convert(song.native, &out, why, sizeof why) == Status::TooSmall);
    CHECK(std::strstr(why, "1604") != nullptr);
    CHECK(out == Bytes({7}));
    song.native.resize(song.native.size() + 1603, 0);
    CHECK(Convert(song.native, &out, why, sizeof why) == Status::TooSmall);
    CHECK(std::strstr(why, "1 byte") != nullptr);
    song.native.push_back(0);
    CHECK(Convert(song.native, &out, why, sizeof why) == Status::Ok);
    CHECK(out == song.stock);
}

TEST(nativesng_every_truncation_is_safe) {
    for (int instrument = 0; instrument < 2; ++instrument) {
        for (int n = instrument ? 4 : 6; n <= (instrument ? 6 : 8); ++n) {
            const Song song(instrument, n);
            Bytes out;
            char why[180];

            for (size_t size = 0; size < song.native.size(); ++size) {
                const auto s = rsxrnative::Convert(song.native.data(), size, &out, why, sizeof why);

                CHECK(size >= song.end ? s != Status::NotNative && s != Status::Bad : s != Status::Ok);
                if (s == Status::Ok) {
                    CHECK(out.size() == size);
                }
            }
        }
    }
}
