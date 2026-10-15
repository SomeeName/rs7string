#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// RS7X SNG extension block (docs/release/RS7X-SPEC.md, issue #18 stage 1): extra-string data
// appended to the SNG body right after the stock MetaData, inside the zlib stream. The stock loader
// SNG_Song_Load 0x558BD0 inflates the whole body (only check: inflated size == header size, 0x558DC9),
// reads the stock sections from a memory stream and returns after MetaData.maxDifficulty (0x55922A)
// without comparing the stream position with its size: trailing bytes are never read (CONFIRMED,
// static + in game). XB1 (0x55922C, before the stream's destructor frees the body) hands us the
// stream: the bytes between its position and size are the block.
//
// The parsed block is kept per song (Rs7Ext_Find): the native N-slot chord data (core/nslot.h) builds
// its T8/C8 records from it (packages without a block: the old name[31] byte, see core/chord7.h).
namespace rs7ext {

constexpr uint32_t kHeaderSize = 16;
constexpr uint32_t kSectionHeader = 12;
constexpr uint16_t kVersion = 1;
constexpr uint16_t kFlagCritical = 1;
constexpr int kMaxStrings = 16;
constexpr int kMaxFret = 24;
constexpr int kMaxBends = 32;

struct Bend {
    float time;
    float step;
    uint32_t reserved;
};

// One extended string inside one ChordNotes record (the stock record's per-string fields).
struct Tech {
    uint32_t mask = 0;
    int8_t slideTo = -1;
    int8_t slideUnpitchTo = -1;
    int16_t vibrato = 0;
    std::vector<Bend> bends;
};

struct Block {
    int instrument = 0;
    int stringCount = 0;
    std::vector<int> strings;          // extended string index per column (7-string: {6})
    bool hasTemplates = false;         // CHDT present (absent: every column -1)
    bool hasChordNotes = false;        // CHNT present (absent: no extended techniques)
    std::vector<int8_t> fret, finger;  // [template * columns + column]
    std::vector<Tech> tech;            // [chordNotes * columns + column]
    int skipped = 0;                   // unknown non-critical sections

    int Columns() const { return static_cast<int>(strings.size()); }

    int Column(int string) const;
    // Extended column `string` of template `t`: false if the string is not extended or not played.
    bool TemplateColumn(uint32_t t, int string, int* fret, int* finger) const;
};

enum class Status {
    None,  // no bytes after MetaData
    Ok,
    NotBlock,     // trailing bytes without the RS7X magic
    Unsupported,  // other version, unknown critical section, another instrument
    Invalid,      // breaks a rule of the spec
};
const char* StatusName(Status s);

// Parses and validates the bytes after MetaData against the loaded song's counts (stock chord
// templates, ChordNotes records, MetaData tuning entries). Anything but Ok leaves *out unspecified
// and `why` holds the reason.
Status Parse(const uint8_t* p, size_t n, uint32_t templates, uint32_t chordNotes, uint32_t tuning, Block* out,
             char* why, size_t whyLen);

}  // namespace rs7ext

// XB1 (loader thread): end of SNG_Song_Load, stock sections read, body still allocated. `song` =
// SNG_Song (templates +94h, ChordNotes +0ACh, tuning +110h), `stream` = the body's memory stream
// (+10h buffer, +14h position, +18h size).
extern "C" __declspec(dllexport) void __cdecl Rs7Ext_OnSngLoaded(void* song, const void* stream);
// The parsed block of a loaded SNG_Song (nullptr: none or not valid). Keeps the last few songs.
std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void* song);
// Leak audit: parsed blocks kept now (at most 8; the oldest goes when a ninth arrives).
int Rs7Ext_StoredBlocks();
// The store behind Rs7Ext_OnSngLoaded (exposed for tests): `b` = nullptr forgets `song`; otherwise
// `song` becomes the newest entry and, at the cap of 8, the oldest entry goes. The just-loaded song is
// always the newest entry, so it is never the one dropped.
void Rs7Ext_Store(const void* song, std::shared_ptr<const rs7ext::Block> b);
constexpr int kRs7ExtKeep = 8;
