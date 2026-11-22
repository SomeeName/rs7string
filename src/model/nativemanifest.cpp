#include "model/nativemanifest.h"
#include "model/slots.h"
#include <algorithm>
#include <cstring>
#include <string_view>
#include <vector>

namespace rsxrnative {
namespace {
constexpr size_t kMaxText = 16u << 20;
constexpr int kMaxDepth = 64;
constexpr int kMaxStrings = 16;
constexpr std::string_view kBom = "\xEF\xBB\xBF";
constexpr std::string_view kMarker = "\"ExtendedRange\"";

struct Member {
    size_t begin = 0;                 // the key's opening quote
    size_t keyBegin = 0, keyEnd = 0;  // the key without its quotes
    size_t valueBegin = 0, valueEnd = 0;
    int object = -1;  // index of the value's Object, -1 when the value is not an object
};

struct Object {
    std::vector<Member> members;
};

struct Edit {
    size_t begin, end;
    std::string text;
};

struct Fail {};

// The objects of a JSON text and their members, as spans of the text. Throws Fail on text it does not accept.
class Scanner {
public:
    Scanner(const char* text, size_t size) : s_(text, size) {}

    std::vector<Object> objects;

    void Run() {
        if (s_.substr(0, kBom.size()) == kBom) {
            at_ = kBom.size();
        }

        Space();
        Value(0);
        Space();

        if (at_ != s_.size()) {
            throw Fail{};
        }
    }

    std::string_view Text(size_t begin, size_t end) const { return s_.substr(begin, end - begin); }

private:
    static bool IsAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    char Peek() const { return at_ < s_.size() ? s_[at_] : '\0'; }

    void Space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\r' || s_[at_] == '\n')) {
            ++at_;
        }
    }

    void Expect(char c) {
        if (Peek() != c) {
            throw Fail{};
        }

        ++at_;
    }

    // A string: at_ on its opening quote, left one past the closing quote.
    void String() {
        Expect('"');

        while (at_ < s_.size() && s_[at_] != '"') {
            at_ += s_[at_] == '\\' ? 2 : 1;
        }

        Expect('"');
    }

    void Array(int depth) {
        ++at_;
        Space();

        if (Peek() == ']') {
            ++at_;
            return;
        }

        for (;;) {
            Value(depth + 1);
            Space();

            if (Peek() != ',') {
                break;
            }

            ++at_;
            Space();
        }

        Expect(']');
    }

    // Returns the Object index of an object value, else -1.
    int Value(int depth) {
        if (depth > kMaxDepth) {
            throw Fail{};
        }

        const char c = Peek();

        if (c == '{') {
            return ObjectValue(depth);
        }

        if (c == '[') {
            Array(depth);
            return -1;
        }

        if (c == '"') {
            String();
            return -1;
        }

        const size_t begin = at_;  // a number or a literal: letters, digits, sign, point (never a quote or bracket)

        while (at_ < s_.size() && (IsAlnum(s_[at_]) || s_[at_] == '-' || s_[at_] == '+' || s_[at_] == '.')) {
            ++at_;
        }

        if (at_ == begin) {
            throw Fail{};
        }

        return -1;
    }

    int ObjectValue(int depth) {
        const size_t index = objects.size();

        objects.emplace_back();
        ++at_;
        Space();

        if (Peek() != '}') {
            for (;;) {
                Member m;

                m.begin = at_;
                String();
                m.keyBegin = m.begin + 1;
                m.keyEnd = at_ - 1;
                Space();
                Expect(':');
                Space();
                m.valueBegin = at_;
                m.object = Value(depth + 1);
                m.valueEnd = at_;
                objects[index].members.push_back(m);
                Space();

                if (Peek() != ',') {
                    break;
                }

                ++at_;
                Space();
            }
        }

        Expect('}');

        return static_cast<int>(index);
    }

    std::string_view s_;
    size_t at_ = 0;
};

const Member* Find(const Scanner& sc, const Object& o, std::string_view key) {
    for (const Member& m : o.members) {
        if (sc.Text(m.keyBegin, m.keyEnd) == key) {
            return &m;
        }
    }

    return nullptr;
}

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

// A tuning offset: a whole number of semitones from -24 to 24, written plainly (no fraction, exponent or leading zero).
bool IsOffset(std::string_view v) {
    if (!v.empty() && v[0] == '-') {
        v.remove_prefix(1);
    }

    if (v.empty() || v.size() > 2 || !IsDigit(v[0]) || (v.size() == 2 && (v[0] == '0' || !IsDigit(v[1])))) {
        return false;
    }

    return (v.size() == 1 ? v[0] - '0' : (v[0] - '0') * 10 + (v[1] - '0')) <= 24;
}

// "stringI" with I in 0..15 and no leading zero: I, else -1.
int StringIndex(std::string_view key) {
    constexpr std::string_view prefix = "string";

    if (key.size() <= prefix.size() || key.size() > prefix.size() + 2 || key.substr(0, prefix.size()) != prefix) {
        return -1;
    }

    const std::string_view n = key.substr(prefix.size());

    if (n.size() == 1) {
        return IsDigit(n[0]) ? n[0] - '0' : -1;
    }

    return n[0] == '1' && n[1] >= '0' && n[1] <= '5' ? 10 + (n[1] - '0') : -1;
}

void Put(std::string* s, const char* key, std::string_view value) {
    if (s->size() > 1) {
        *s += ", ";
    }

    *s += '"';
    *s += key;
    *s += "\": ";
    s->append(value.data(), value.size());
}

// The internal Tuning of a native arrangement; it is a bass when its ArrangementName is "Bass", as in a stock
// manifest, and a guitar otherwise. *valid comes in as "may be valid" and goes out false when the
// arrangement is not a valid native one; the result is then the harmless six-entry form.
std::string Tuning(const Scanner& sc, const Object& arrangement, const Object& marker, const Object& tuning,
                   bool* valid) {
    static const char* const kStock[6] = {"string0", "string1", "string2", "string3", "string4", "string5"};
    std::string_view native[kMaxStrings];
    bool seen[kMaxStrings]{};
    const int count = static_cast<int>(std::min<size_t>(tuning.members.size(), kMaxStrings + 1));
    const Member* version = Find(sc, marker, "Version");
    const Member* name = Find(sc, arrangement, "ArrangementName");
    const bool bass = name && sc.Text(name->valueBegin, name->valueEnd) == "\"Bass\"";

    *valid = *valid && version && sc.Text(version->valueBegin, version->valueEnd) == "1" &&
             rsxr::NativeSlot(bass, count, 0) >= 0;

    for (const Member& m : tuning.members) {
        const int i = StringIndex(sc.Text(m.keyBegin, m.keyEnd));
        const std::string_view value = sc.Text(m.valueBegin, m.valueEnd);

        if (i < 0 || i >= count || i >= kMaxStrings || seen[i] || !IsOffset(value)) {
            *valid = false;
            continue;
        }

        seen[i] = true;
        native[i] = value;
    }

    std::string_view slots[8];

    for (std::string_view& v : slots) {
        v = "0";
    }

    for (int i = 0; i < count && i < kMaxStrings; ++i) {
        const int slot = *valid ? rsxr::NativeSlot(bass, count, i) : i < 6 ? i : -1;

        if (slot >= 0 && seen[i]) {
            slots[slot] = native[i];
        }
    }

    std::string out = "{";

    for (int s = 0; s < 6; ++s) {
        Put(&out, kStock[s], slots[s]);
    }

    if (*valid && bass && count >= 5) {
        Put(&out, "RsxrString4", slots[4]);
    }

    if (*valid && bass && count == 6) {
        Put(&out, "RsxrString5", slots[5]);
    }

    if (*valid && !bass && count >= 7) {
        Put(&out, "RsxrString6", slots[6]);
    }

    if (*valid && !bass && count == 8) {
        Put(&out, "RsxrString7", slots[7]);
    }

    return out + "}";
}

void ToneMarker(const Scanner& sc, const Object& o, const Member& marker, bool valid, std::vector<Edit>& edits,
                size_t markerEdit) {
    bool extraTone = false, reserved = false;

    for (const Member& m : o.members) {
        const std::string_view key = sc.Text(m.keyBegin, m.keyEnd);
        const std::string_view value = sc.Text(m.valueBegin, m.valueEnd);

        extraTone |= key.size() == 6 && key.substr(0, 5) == "Tone_" && key[5] >= 'E' && key[5] <= 'Z' &&
                     value.size() > 2 && value.front() == '"' && value.back() == '"';

        if (key == "RsxrToneSlots") {
            reserved = true;
            edits.push_back({m.valueBegin, m.valueEnd, "0"});
        }
    }

    if (valid && extraTone && !reserved) {
        edits[markerEdit] = {marker.begin, marker.valueEnd, "\"RsxrToneSlots\": 26"};
    }
}

bool Rewrite(const char* text, size_t size, std::string* out, ManifestStats* stats) {
    static const Object kNone;
    Scanner sc(text, size);

    sc.Run();

    std::vector<Edit> edits;
    ManifestStats found;

    for (const Object& o : sc.objects) {
        const Member* marker = Find(sc, o, "ExtendedRange");

        if (!marker) {
            continue;
        }

        // Remove the authoring marker; an eligible tone arrangement replaces this edit below.
        const size_t markerEdit = edits.size();
        const size_t index = static_cast<size_t>(marker - o.members.data());

        if (index + 1 < o.members.size()) {
            edits.push_back({marker->begin, o.members[index + 1].begin, {}});
        } else if (index > 0) {
            edits.push_back({o.members[index - 1].valueEnd, marker->valueEnd, {}});
        } else {
            edits.push_back({marker->begin, marker->valueEnd, {}});
        }

        // Every Tuning member is rewritten (the game's parser may keep the first or the last of a repeated key);
        // the arrangement is valid only with exactly one, an object.
        const Object& info = marker->object >= 0 ? sc.objects[static_cast<size_t>(marker->object)] : kNone;
        size_t tunings = 0;

        for (const Member& m : o.members) {
            tunings += sc.Text(m.keyBegin, m.keyEnd) == "Tuning";
        }

        bool valid = tunings == 1;

        for (const Member& m : o.members) {
            if (sc.Text(m.keyBegin, m.keyEnd) != "Tuning") {
                continue;
            }

            if (m.object < 0) {
                valid = false;
                continue;
            }

            bool one = tunings == 1;
            std::string current = Tuning(sc, o, info, sc.objects[static_cast<size_t>(m.object)], &one);

            valid = valid && one;
            edits.push_back({m.valueBegin, m.valueEnd, std::move(current)});
        }

        ToneMarker(sc, o, *marker, valid, edits, markerEdit);

        ++(valid ? found.rewritten : found.bad);
    }

    if (edits.empty()) {
        return false;
    }

    // Two edits overlap only when a marked object sits inside another one's marker or Tuning value: the spans of
    // a JSON text nest, so the inner edit lies inside the outer one, which replaces it. Applied back to front, so
    // earlier spans keep their positions.
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });

    std::string result(text, size);
    size_t kept = 0;

    for (size_t i = 1; i < edits.size(); ++i) {
        if (edits[i].begin >= edits[kept].end) {
            edits[++kept] = std::move(edits[i]);
        }
    }

    for (size_t i = kept + 1; i-- > 0;) {
        result.replace(edits[i].begin, edits[i].end - edits[i].begin, edits[i].text);
    }

    *out = std::move(result);

    if (stats) {
        *stats = found;
    }

    return true;
}
}  // namespace

size_t JsonExtent(const char* text, size_t limit) {
    if (!text) {
        return 0;
    }

    size_t at = 0;

    // Byte by byte: a two-byte "{}" need not have a third byte behind it.
    if (limit >= kBom.size() && text[0] == kBom[0] && text[1] == kBom[1] && text[2] == kBom[2]) {
        at = kBom.size();
    }

    while (at < limit && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) {
        ++at;
    }

    if (at >= limit || (text[at] != '{' && text[at] != '[')) {
        return 0;
    }

    size_t depth = 0;
    bool inString = false;

    for (; at < limit && text[at]; ++at) {
        const char c = text[at];

        if (inString) {
            if (c == '\\') {
                if (++at >= limit || !text[at]) {
                    return 0;
                }
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if ((c == '}' || c == ']') && --depth == 0) {
            return at + 1;
        }
    }

    return 0;
}

bool RewriteManifest(const char* text, size_t size, std::string* out, ManifestStats* stats) {
    if (!text || !out || size > kMaxText || std::string_view(text, size).find(kMarker) == std::string_view::npos) {
        return false;
    }

    try {
        return Rewrite(text, size, out, stats);
    } catch (...) {
        return false;  // not JSON this scanner accepts, or out of memory: the game parses the original
    }
}
}  // namespace rsxrnative
