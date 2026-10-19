#include "model/extblock.h"
#include "test.h"
#include <cstring>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
using rs7ext::Block;
using rs7ext::Status;

constexpr size_t kHeaderSizeOffset = 6;
constexpr size_t kBlockSizeOffset = 8;
constexpr size_t kSectionCountOffset = 12;
constexpr size_t kSectionSizeOffset = 8;
constexpr size_t kStrsPayload = rs7ext::kHeaderSize + rs7ext::kSectionHeader;
constexpr size_t kStrsStringCount = kStrsPayload + 1;
constexpr size_t kStrsColumnCount = kStrsPayload + 2;
constexpr size_t kStrsColumns = kStrsPayload + 4;

void U32(Bytes& b, size_t at, uint32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}

// Synthetic wire data only. Sizes and fields follow RS7X-SPEC, sections 2/3.
Bytes Header() {
    return {'R', 'S', '7', 'X', 1, 0, 16, 0, 16, 0, 0, 0, 0, 0, 0, 0};
}

size_t Section(Bytes& b, const char* id, const Bytes& payload, uint8_t version = 1, uint8_t flags = 0) {
    const size_t at = b.size();

    b.insert(b.end(), id, id + 4);
    b.insert(b.end(), {version, 0, flags, 0, 0, 0, 0, 0});
    U32(b, at + kSectionSizeOffset, static_cast<uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
    U32(b, kBlockSizeOffset, static_cast<uint32_t>(b.size()));
    uint32_t count;

    std::memcpy(&count, b.data() + kSectionCountOffset, 4);
    U32(b, kSectionCountOffset, count + 1);

    return at;
}

Bytes Strings(int n = 7) {
    Bytes b = Header();
    Bytes p = {0, static_cast<uint8_t>(n), static_cast<uint8_t>(n - 6), 0};

    for (int s = 6; s < n; ++s) {
        p.push_back(static_cast<uint8_t>(s));
    }

    Section(b, "STRS", p, 1, 1);

    return b;
}

Status Parse(const Bytes& b, Block* out, uint32_t templates = 0, uint32_t notes = 0, uint32_t tuning = 7) {
    return rs7ext::Parse(b.data(), b.size(), templates, notes, tuning, out, nullptr, 0);
}
}  // namespace

TEST(extblock_edges_reject_every_incomplete_prefix_even_with_matching_block_size) {
    Bytes full = Strings();

    Section(full, "CHDT", {1, 0, 0, 0, 24, 4});
    Section(full, "CHNT", {1, 0, 0, 0, 0, 0x20, 0, 0, 24, 0xFF, 0, 0, 0});
    Block out;

    CHECK(Parse(full, &out, 1, 1) == Status::Ok);

    for (size_t n = 1; n < full.size(); ++n) {
        Bytes b(full.begin(), full.begin() + n);

        if (n >= 16) {
            U32(b, kBlockSizeOffset, static_cast<uint32_t>(n));
        }
        CHECK(Parse(b, &out, 1, 1) == (n < 4 ? Status::NotBlock : Status::Invalid));
    }
}

TEST(extblock_edges_empty_song_and_missing_optional_sections) {
    Block out;

    CHECK(Parse(Strings(), &out) == Status::Ok);
    CHECK(!out.hasTemplates && !out.hasChordNotes && out.fret.empty() && out.tech.empty());
    auto b = Strings();

    Section(b, "CHDT", {0, 0, 0, 0});
    Section(b, "CHNT", {0, 0, 0, 0});
    CHECK(Parse(b, &out) == Status::Ok);
    CHECK(out.hasTemplates && out.hasChordNotes && out.fret.empty() && out.tech.empty());
}

TEST(extblock_edges_eight_string_columns_are_independent) {
    auto b = Strings(8);

    Section(b, "CHDT", {2, 0, 0, 0, 2, 2, 0, 0xFF, 0xFF, 0xFF, 24, 4});
    Block out;

    CHECK(Parse(b, &out, 2, 0, 8) == Status::Ok);
    int fret = 99, finger = 99;

    CHECK(out.TemplateColumn(0, 6, &fret, &finger) && fret == 2 && finger == 2);
    CHECK(out.TemplateColumn(0, 7, &fret, &finger) && fret == 0 && finger == -1);
    CHECK(out.TemplateColumn(1, 7, &fret, &finger) && fret == 24 && finger == 4);
    CHECK(!out.TemplateColumn(1, 6, &fret, &finger));
    CHECK(fret == 24 && finger == 4);
    CHECK(!out.TemplateColumn(2, 7, &fret, &finger));
    CHECK(!out.TemplateColumn(UINT32_MAX, 7, &fret, &finger));
    CHECK(!out.TemplateColumn(0, -1, &fret, &finger));
}

TEST(extblock_edges_reentry_replaces_previous_successful_block) {
    auto b = Strings(8);

    Section(b, "CHDT", {1, 0, 0, 0, 2, 1, 3, 2});
    Block out;

    CHECK(Parse(b, &out, 1, 0, 8) == Status::Ok);
    CHECK(Parse(Strings(), &out) == Status::Ok);
    CHECK(out.stringCount == 7 && out.Columns() == 1 && out.Column(7) == -1);
    CHECK(!out.hasTemplates && out.fret.empty() && out.finger.empty() && out.tech.empty());
}

TEST(extblock_edges_header_extensions_are_skipped) {
    auto b = Strings();

    b.insert(b.begin() + 16, {0xAA, 0xBB, 0xCC});
    b[kHeaderSizeOffset] = 19;
    U32(b, kBlockSizeOffset, static_cast<uint32_t>(b.size()));
    Block out;

    CHECK(Parse(b, &out) == Status::Ok && out.stringCount == 7);

    for (uint8_t size : {uint8_t(15), uint8_t(255)}) {
        b[kHeaderSizeOffset] = size;
        CHECK(Parse(b, &out) == Status::Invalid);
    }
}

TEST(extblock_edges_declared_sizes_cannot_wrap_or_request_giant_arrays) {
    Block out;

    for (const char* id : {"CHDT", "CHNT"}) {
        auto b = Strings();

        const size_t section = Section(b, id, {0xFF, 0xFF, 0xFF, 0xFF});

        CHECK(Parse(b, &out, UINT32_MAX, UINT32_MAX) == Status::Invalid);
        U32(b, section + kSectionSizeOffset, UINT32_MAX);
        CHECK(Parse(b, &out, UINT32_MAX, UINT32_MAX) == Status::Invalid);
    }

    auto b = Strings();

    U32(b, kSectionCountOffset, UINT32_MAX);
    CHECK(Parse(b, &out) == Status::Invalid);
}

TEST(extblock_edges_template_fret_and_finger_boundaries) {
    Block out;

    for (uint8_t fret : {uint8_t(0xFE), uint8_t(25), uint8_t(127)}) {
        auto b = Strings();

        Section(b, "CHDT", {1, 0, 0, 0, fret, 0xFF});
        CHECK(Parse(b, &out, 1) == Status::Invalid);
    }

    for (uint8_t finger : {uint8_t(0xFE), uint8_t(5), uint8_t(127)}) {
        auto b = Strings();

        Section(b, "CHDT", {1, 0, 0, 0, 0, finger});
        CHECK(Parse(b, &out, 1) == Status::Invalid);
    }
}

TEST(extblock_edges_bend_capacity_and_truncated_bend) {
    Bytes payload = {1, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 32};

    for (int i = 0; i < 32; ++i) {
        // {time=1.0f, step=0.5f, reserved=0}, hand-encoded IEEE values.
        payload.insert(payload.end(), {0, 0, 0x80, 0x3F, 0, 0, 0, 0x3F, 0, 0, 0, 0});
    }

    auto b = Strings();

    Section(b, "CHNT", payload);
    Block out;

    CHECK(Parse(b, &out, 0, 1) == Status::Ok);
    CHECK(out.tech.size() == 1 && out.tech[0].bends.size() == 32);
    CHECK(out.tech[0].bends.back().time == 1.0f && out.tech[0].bends.back().step == 0.5f);
    payload.pop_back();
    b = Strings();
    Section(b, "CHNT", payload);
    CHECK(Parse(b, &out, 0, 1) == Status::Invalid);
    payload.push_back(0);  // restore the truncated 32nd bend
    payload.insert(payload.end(), {0, 0, 0x80, 0x3F, 0, 0, 0, 0x3F, 0, 0, 0, 0});
    payload[12] = 33;
    b = Strings();
    Section(b, "CHNT", payload);
    CHECK(Parse(b, &out, 0, 1) == Status::Invalid);
}

TEST(extblock_edges_unsupported_optional_version_does_not_publish_techniques) {
    auto b = Strings();

    Section(b, "CHNT", {0xFF}, 2);
    Block out;

    CHECK(Parse(b, &out) == Status::Ok);
    CHECK(out.skipped == 1 && !out.hasChordNotes && out.tech.empty());
    b = Header();
    Section(b, "STRS", {0, 7, 1, 0, 6}, 2);
    CHECK(Parse(b, &out) == Status::Invalid);  // a skipped STRS cannot supply the required string layout
}

TEST(extblock_edges_duplicate_sections_are_invalid_for_supported_and_skipped_versions) {
    for (const char* id : {"STRS", "CHDT", "CHNT", "FUTR"}) {
        for (uint8_t first : {uint8_t(1), uint8_t(2)}) {
            if (std::strcmp(id, "STRS") == 0 && first != 1) {
                continue;  // the initial STRS from Strings() is always supported
            }

            for (uint8_t second : {uint8_t(1), uint8_t(2)}) {
                auto b = Strings();
                const bool strs = std::strcmp(id, "STRS") == 0;
                const Bytes payload = strs ? Bytes{0, 7, 1, 0, 6} : Bytes{0, 0, 0, 0};

                if (!strs) {
                    Section(b, id, payload, first);
                }

                Section(b, id, payload, second);
                Block out;

                CHECK(Parse(b, &out) == Status::Invalid);
            }
        }
    }
}

TEST(extblock_edges_many_optional_sections_still_reject_a_late_duplicate) {
    auto b = Strings();

    for (uint8_t i = 0; i < 24; ++i) {
        const char id[4] = {'F', 'U', 'T', static_cast<char>(i)};

        Section(b, id, {});
    }

    Block out;

    CHECK(Parse(b, &out) == Status::Ok && out.skipped == 24);
    const char duplicate[4] = {'F', 'U', 'T', 23};

    Section(b, duplicate, {});
    CHECK(Parse(b, &out) == Status::Invalid);
}

TEST(extblock_edges_reason_buffer_is_bounded_and_optional) {
    auto b = Strings();

    b[kStrsStringCount] = 6;
    Block out;
    char why[3] = {'x', 'x', 'z'};

    CHECK(rs7ext::Parse(b.data(), b.size(), 0, 0, 7, &out, why, 2) == Status::Invalid);
    CHECK(why[0] != 0 && why[1] == 0 && why[2] == 'z');
    CHECK(rs7ext::Parse(b.data(), b.size(), 0, 0, 7, &out, why, 0) == Status::Invalid);
    CHECK(why[2] == 'z');
    CHECK(rs7ext::Parse(nullptr, 1, 0, 0, 7, &out, nullptr, 0) == Status::Invalid);
    CHECK(rs7ext::Parse(b.data(), b.size(), 0, 0, 7, nullptr, nullptr, 0) == Status::Invalid);
}

TEST(extblock_edges_string_counts_and_column_order_are_validated) {
    Block out;

    for (uint8_t n : {uint8_t(0), uint8_t(4), uint8_t(6), uint8_t(17), uint8_t(255)}) {
        auto b = Strings();

        b[kStrsStringCount] = n;
        CHECK(Parse(b, &out, 0, 0, n) == Status::Invalid);
    }

    auto b = Strings(16);

    CHECK(Parse(b, &out, 0, 0, 16) == Status::Ok && out.Columns() == 10 && out.Column(15) == 9);
    b = Strings(8);
    b[kStrsColumns + 1] = 6;  // repeated column instead of ordered 6,7
    CHECK(Parse(b, &out, 0, 0, 8) == Status::Invalid);
    b = Strings();
    b[kStrsColumnCount] = 0;
    CHECK(Parse(b, &out) == Status::Invalid);
}

TEST(extblock_edges_slide_frets_and_trailing_payload_are_validated) {
    Block out;

    for (size_t at : {size_t(8), size_t(9)}) {
        for (uint8_t fret : {uint8_t(0xFE), uint8_t(25), uint8_t(127)}) {
            Bytes tech = {1, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0};

            tech[at] = fret;
            auto b = Strings();

            Section(b, "CHNT", tech);
            CHECK(Parse(b, &out, 0, 1) == Status::Invalid);
        }
    }

    auto b = Strings();

    Section(b, "CHNT", {0, 0, 0, 0, 0});
    CHECK(Parse(b, &out) == Status::Invalid);
    b = Strings();
    b.push_back(0);
    U32(b, kBlockSizeOffset, static_cast<uint32_t>(b.size()));
    CHECK(Parse(b, &out) == Status::Invalid);
}
