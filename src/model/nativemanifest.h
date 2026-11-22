#pragma once
#include <cstddef>
#include <string>

// The native manifest form of docs/NATIVE-FORMAT.md: an arrangement's attributes carry
//   "ExtendedRange": {"Version": 1}
// and a "Tuning" object with one "stringI" per string, thickest first; "ArrangementName": "Bass" makes it a bass,
// as in a stock manifest. The game and the rest of the mod read the
// internal form (string0-string5 in the game's slot order plus the RsxrString keys), so the text is rewritten before
// the game's JSON parser sees it. Pure text in, text out; no game memory.
namespace rsxrnative {
struct ManifestStats {
    int rewritten = 0;  // native arrangements rewritten
    int bad = 0;        // marked native but not valid: made harmless (a six-entry Tuning), see RewriteManifest
};

// The length of the JSON object or array at `text`, a leading UTF-8 byte-order mark and white space included: the
// bytes the game's parser will read. 0 when no object or array starts there or it does not end within `limit` bytes
// or before a zero byte. Reads no byte after the value's closing bracket (a game buffer is not always
// zero-terminated right behind it).
size_t JsonExtent(const char* text, size_t limit);

// False when `text` (a manifest .json or .hsan, `size` bytes, no terminator needed) has no native arrangement or is
// not JSON this scanner accepts: *out is untouched and the game parses the original. True: *out is the whole text
// with every native arrangement in the internal form and the marker removed. Valid arrangements with nonempty
// Tone_E..Tone_Z names get the internal RsxrToneSlots=26 field for the tone loader; writers never emit it.
// An arrangement that is marked but
// invalid (unknown version, a string count the instrument does not have, Tuning keys that are not
// exactly string0..stringN-1 with number values) gets a six-entry Tuning from what is there, so the stock readers
// never meet a seventh "stringI" key; it counts in stats->bad. No C++ exception escapes.
bool RewriteManifest(const char* text, size_t size, std::string* out, ManifestStats* stats = nullptr);
}  // namespace rsxrnative
