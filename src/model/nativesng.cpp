#include "model/nativesng.h"
#include "model/slots.h"
#include <cstdio>
#include <cstring>
#include <new>

namespace rsxrnative {
namespace {
struct Error {
    const char* field;
    size_t offset;
};

// Every span and count is bounded before pointer arithmetic or multiplication. Names are static strings,
// so reporting a malformed input never allocates. SNG array counts are signed int32 on the wire.
struct Reader {
    const uint8_t* body;
    size_t size, at = 0;
    const char* field = "header";

    const uint8_t* Take(size_t bytes) {
        if (bytes > size - at) {
            throw Error{field, at};
        }

        const uint8_t* p = body + at;

        at += bytes;

        return p;
    }

    template <class T>
    T Get() {
        T value;

        std::memcpy(&value, Take(sizeof value), sizeof value);

        return value;
    }

    uint32_t Count(size_t minimum) {
        const size_t offset = at;
        const uint32_t count = Get<uint32_t>();
        if (count > INT32_MAX || count > (size - at) / minimum) {
            throw Error{field, offset};
        }

        return count;
    }
};

template <class T>
T Load(const uint8_t* p) {
    T value;

    std::memcpy(&value, p, sizeof value);

    return value;
}

struct Header {
    int instrument, strings;
    int slot[8]{};

    bool Extended() const { return strings > (instrument ? 4 : 6); }

    int Tuning() const { return instrument ? 6 : strings; }

    int Columns() const { return instrument ? 0 : strings - 6; }
};

Header ReadHeader(Reader& r) {
    r.Take(4);
    if (r.Get<uint8_t>() != 1) {
        throw Error{"header version", 4};
    }

    const int instrument = r.Get<uint8_t>(), n = r.Get<uint8_t>();
    if (instrument > 1) {
        throw Error{"header instrument", 5};
    }
    if (rsxr::NativeSlot(instrument != 0, n, 0) < 0) {
        throw Error{"header string count", 6};
    }
    if (r.Get<uint8_t>() != 0) {
        throw Error{"header reserved", 7};
    }

    Header h{instrument, n};

    for (int i = 0; i < n; ++i) {
        h.slot[i] = rsxr::NativeSlot(instrument != 0, n, i);
    }

    return h;
}

// A sizing pass precedes allocation. uint64_t keeps the exact missing-byte count even on Win32 when
// expanding a near-4GB bass body. The writing pass additionally bounds every write and patch.
struct Writer {
    uint8_t* body = nullptr;
    size_t capacity = 0;
    uint64_t at = 0;

    void Bytes(const void* p, size_t bytes) {
        if (body) {
            if (at > capacity || bytes > capacity - static_cast<size_t>(at)) {
                throw Error{"output bounds", 0};
            }

            if (bytes) {
                std::memcpy(body + static_cast<size_t>(at), p, bytes);
            }
        }

        at += bytes;
    }

    template <class T>
    void Put(T value) {
        Bytes(&value, sizeof value);
    }

    void Patch(uint64_t offset, uint32_t value) {
        if (body) {
            if (offset > capacity || sizeof value > capacity - static_cast<size_t>(offset)) {
                throw Error{"output patch bounds", 0};
            }

            std::memcpy(body + static_cast<size_t>(offset), &value, sizeof value);
        }
    }

    uint64_t Section(const char* id, uint16_t flags) {
        Bytes(id, 4);
        Put<uint16_t>(1);
        Put(flags);
        const uint64_t offset = at;

        Put<uint32_t>(0);

        return offset;
    }

    void EndSection(uint64_t offset) { Patch(offset, static_cast<uint32_t>(at - offset - 4)); }
};

struct Converter {
    Reader r;
    Header h;
    Writer w;
    uint32_t templates = 0, chordNotes = 0;
    const uint8_t* templateData = nullptr;
    const uint8_t* techniqueData = nullptr;

    void Copy(size_t bytes) { w.Bytes(r.Take(bytes), bytes); }

    uint32_t Count(const char* field, size_t minimum) {
        r.field = field;
        const uint32_t count = r.Count(minimum);

        w.Put(count);

        return count;
    }

    uint32_t Fixed(const char* field, size_t stride) {
        const uint32_t count = Count(field, stride);

        Copy(static_cast<size_t>(count) * stride);

        return count;
    }

    void Templates() {
        const size_t stride = 36 + 6 * h.strings;

        templates = Count("chord templates", stride);
        templateData = r.body + r.at;

        for (uint32_t row = 0; row < templates; ++row) {
            const uint8_t* p = r.Take(stride);
            uint8_t stock[72]{};

            std::memcpy(stock, p, 4);
            std::memset(stock + 4, 255, 36);  // unused columns: fret, finger and note are all -1

            for (int i = 0; i < h.strings; ++i) {
                const int slot = h.slot[i];

                if (slot < 6) {
                    stock[4 + slot] = p[4 + i];
                    stock[10 + slot] = p[4 + h.strings + i];
                    std::memcpy(stock + 16 + 4 * slot, p + 4 + 2 * h.strings + 4 * i, 4);
                } else {
                    const int fret = Load<int8_t>(p + 4 + i), finger = Load<int8_t>(p + 4 + h.strings + i);
                    if (fret < -1 || fret > 24 || finger < -1 || finger > 4 || (fret == -1 && finger != -1)) {
                        throw Error{"chord template extra fret/finger", static_cast<size_t>(p - r.body + 4 + i)};
                    }
                }
            }

            std::memcpy(stock + 40, p + 4 + 6 * h.strings, 32);
            w.Bytes(stock, sizeof stock);
        }
    }

    void Techniques() {
        const int n = h.strings;
        const size_t stride = 396 * n;

        chordNotes = Count("ChordNotes", stride);
        techniqueData = r.body + r.at;

        for (uint32_t row = 0; row < chordNotes; ++row) {
            const uint8_t* p = r.Take(stride);
            uint8_t stock[2376]{};

            std::memset(stock + 2352, 255, 12);

            for (int i = 0; i < n; ++i) {
                const int slot = h.slot[i];
                const size_t bend = 4 * n + 388 * i, slide = 392 * n + i;
                if (Load<uint32_t>(p + bend + 384) > 32) {
                    throw Error{"ChordNotes bend count", static_cast<size_t>(p - r.body + bend + 384)};
                }

                if (slot < 6) {
                    std::memcpy(stock + 4 * slot, p + 4 * i, 4);
                    std::memcpy(stock + 24 + 388 * slot, p + bend, 388);
                    stock[2352 + slot] = p[slide];
                    stock[2358 + slot] = p[slide + n];
                    std::memcpy(stock + 2364 + 2 * slot, p + 394 * n + 2 * i, 2);
                } else {
                    const int a = Load<int8_t>(p + slide), b = Load<int8_t>(p + slide + n);
                    if (a < -1 || a > 24 || b < -1 || b > 24) {
                        throw Error{"ChordNotes extra slide", static_cast<size_t>(p - r.body + slide)};
                    }
                }
            }

            w.Bytes(stock, sizeof stock);
        }
    }

    void LinkedDifficulties() {
        const uint32_t count = Count("linked difficulties", 8);

        for (uint32_t i = 0; i < count; ++i) {
            Copy(4);
            Fixed("linked difficulty phrases", 4);
        }
    }

    void Sections() {
        const uint32_t count = Count("sections", 88);

        for (uint32_t i = 0; i < count; ++i) {
            Copy(52);
            const uint8_t* masks = r.Take(36);

            for (int mask = 0; mask < 36; ++mask) {
                if (masks[mask] >> h.strings) {
                    throw Error{"section string mask", r.at - 36 + mask};
                }

                uint8_t mapped = 0;

                for (int s = 0; s < h.strings; ++s) {
                    if (masks[mask] & (1 << s)) {
                        mapped |= static_cast<uint8_t>(1 << h.slot[s]);
                    }
                }

                w.Put(mapped);
            }
        }
    }

    void Notes() {
        const uint32_t count = Count("level notes", 67);

        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* p = r.Take(63);

            w.Bytes(p, 16);
            const int string = Load<int8_t>(p + 16);
            if (string < -1 || string >= h.strings) {
                throw Error{"note string index", r.at - 63 + 16};
            }

            w.Put<int8_t>(static_cast<int8_t>(string == -1 ? -1 : h.slot[string]));
            w.Bytes(p + 17, 46);
            Fixed("note bends", 12);
        }
    }

    void Levels() {
        const uint32_t count = Count("levels", 36);

        for (uint32_t i = 0; i < count; ++i) {
            r.field = "level difficulty";
            Copy(4);
            Fixed("level anchors", 28);
            Fixed("level anchor extensions", 12);
            Fixed("level handshapes", 20);
            Fixed("level arpeggios", 20);
            Notes();
            Fixed("level average notes", 4);
            Fixed("level notes excluding ignored", 4);
            Fixed("level notes all", 4);
        }
    }

    void Metadata() {
        r.field = "MetaData";
        Copy(79);
        const size_t offset = r.at;
        if (r.Get<uint32_t>() != static_cast<uint32_t>(h.strings)) {
            throw Error{"MetaData tuning count", offset};
        }

        const uint8_t* p = r.Take(2 * h.strings);
        int16_t tuning[8]{};

        for (int i = 0; i < h.strings; ++i) {
            tuning[h.slot[i]] = Load<int16_t>(p + 2 * i);
        }

        w.Put<uint32_t>(h.Tuning());
        w.Bytes(tuning, 2 * h.Tuning());
        Copy(12);

        for (size_t i = r.at; i < r.size; ++i) {
            if (r.body[i] != 0) {
                throw Error{"nonzero padding", i};
            }
        }
    }

    int Native(int slot) const {
        for (int i = 0; i < h.strings; ++i) {
            if (h.slot[i] == slot) {
                return i;
            }
        }

        throw Error{"missing native column", 12};
    }

    void ExtraTemplates() {
        const uint64_t section = w.Section("CHDT", 1);

        w.Put(templates);

        for (uint32_t row = 0; row < templates; ++row) {
            const uint8_t* p = templateData + static_cast<size_t>(row) * (36 + 6 * h.strings);

            for (int slot = 6; slot < h.strings; ++slot) {
                const int i = Native(slot);

                w.Put(p[4 + i]);
                w.Put(p[4 + h.strings + i]);
            }
        }

        w.EndSection(section);
    }

    void ExtraTechniques() {
        const uint64_t section = w.Section("CHNT", 0);

        w.Put(chordNotes);

        for (uint32_t row = 0; row < chordNotes; ++row) {
            const int n = h.strings;
            const uint8_t* p = techniqueData + static_cast<size_t>(row) * (396 * n);

            for (int slot = 6; slot < n; ++slot) {
                const int i = Native(slot);
                const uint8_t* bends = p + 4 * n + 388 * i;
                const uint8_t used = static_cast<uint8_t>(Load<uint32_t>(bends + 384));

                w.Bytes(p + 4 * i, 4);
                w.Put(p[392 * n + i]);
                w.Put(p[393 * n + i]);
                w.Bytes(p + 394 * n + 2 * i, 2);
                w.Put(used);
                w.Bytes(bends, 12 * used);
            }
        }

        w.EndSection(section);
    }

    void Extension() {
        const uint64_t start = w.at;

        w.Bytes("RSXR", 4);
        w.Put<uint16_t>(1);
        w.Put<uint16_t>(16);
        w.Put<uint32_t>(w.body ? static_cast<uint32_t>(w.capacity - static_cast<size_t>(start)) : 0);
        w.Put<uint32_t>(h.instrument ? 2 : 4);
        const uint64_t strs = w.Section("STRS", 1);

        w.Put<uint8_t>(static_cast<uint8_t>(h.instrument));
        w.Put<uint8_t>(static_cast<uint8_t>(h.strings));
        w.Put<uint8_t>(static_cast<uint8_t>(h.Columns()));
        w.Put<uint8_t>(0);

        for (int slot = 6; slot < h.strings && !h.instrument; ++slot) {
            w.Put<uint8_t>(static_cast<uint8_t>(slot));
        }

        w.EndSection(strs);

        if (!h.instrument) {
            ExtraTemplates();
            ExtraTechniques();
        }

        const uint64_t pad = w.Section("PAD0", 0);

        if (w.body) {
            // The result was zero-initialized; all remaining bytes are the non-critical payload.
            w.Patch(pad, static_cast<uint32_t>(w.capacity - static_cast<size_t>(w.at)));
        }
    }

    uint64_t Run() {
        Fixed("beats", 16);
        Fixed("phrases", 44);
        Templates();
        Techniques();

        if (Fixed("vocals", 60)) {
            Fixed("symbol headers", 32);
            Fixed("symbol textures", 144);
            Fixed("symbol definitions", 44);
        }

        Fixed("phrase iterations", 24);
        Fixed("phrase extra info", 16);
        LinkedDifficulties();
        Fixed("actions", 260);
        Fixed("events", 260);
        Fixed("tones", 8);
        Fixed("DNA", 8);
        Sections();
        Levels();
        Metadata();
        if (h.Extended()) {
            Extension();
        }

        return w.at;
    }
};

Status Fail(char* why, size_t cap, const char* field, size_t offset) {
    if (why && cap) {
        std::snprintf(why, cap, "%s at byte %zu", field, offset);
    }

    return Status::Bad;
}
}  // namespace

bool IsNative(const uint8_t* body, size_t size) {
    return body && size >= 4 && std::memcmp(body, "SNXR", 4) == 0;
}

Status Convert(const uint8_t* body, size_t size, std::vector<uint8_t>* out, char* why, size_t whyCap) {
    if (why && whyCap) {
        why[0] = 0;
    }
    if (!IsNative(body, size)) {
        return Status::NotNative;
    }
    if (!out || size > UINT32_MAX) {
        return Fail(why, whyCap, "null output or body exceeds u32 format limit", 0);
    }

    try {
        Reader input{body, size};
        const Header header = ReadHeader(input);
        Converter sizing{input, header, {}};
        const uint64_t required = sizing.Run();

        if (required > size) {
            if (why && whyCap) {
                std::snprintf(why, whyCap, "missing %llu bytes of padding",
                              static_cast<unsigned long long>(required - size));
            }
            return Status::TooSmall;
        }

        std::vector<uint8_t> converted(size, 0);
        Converter writing{input, header, {converted.data(), converted.size()}};

        writing.Run();
        out->swap(converted);
        return Status::Ok;
    } catch (const Error& error) {
        return Fail(why, whyCap, error.field, error.offset);
    } catch (const std::bad_alloc&) {
        return Fail(why, whyCap, "allocation failure", 0);
    } catch (...) {
        return Fail(why, whyCap, "conversion exception", 0);
    }
}
}  // namespace rsxrnative
