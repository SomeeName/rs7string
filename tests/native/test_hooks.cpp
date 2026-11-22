// The native chart hooks (features/native/hooks.cpp). XI1: an SNXR body is converted in place before the stock
// parser; one that cannot be converted continues at the loader's failure exit; any other body is left alone. JM1: a
// manifest text with native arrangements is parsed from a rewritten copy. PN1: the PSXR package magic becomes PSAR.
#include "platform/midhook.h"
#include "test.h"
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" void __cdecl Rsxr_Mid_XI1(RsxrMid::Context&);
extern "C" void __cdecl Rsxr_Mid_JM1(RsxrMid::Context&);
extern "C" void __cdecl Rsxr_Mid_PN1(RsxrMid::Context&);
extern "C" uintptr_t g_rsxr_resume_XI1;

namespace {
using Bytes = std::vector<uint8_t>;

uint32_t Addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The stock bytes around the site: `jne rel32` (to code + 40h), `mov ebx,[ebp+8]`, the two displaced LEAs, resume.
struct Site {
    uint8_t code[0x48]{};
    uint32_t failure;

    Site() {
        const uint8_t stock[] = {0x0F, 0x85, 0, 0, 0, 0, 0x8B, 0x5D, 0x08, 0x8D, 0x54, 0x24, 0x14, 0x8D, 0x43, 0x34};
        const int32_t rel = 0x40 - 6;

        std::memcpy(code, stock, sizeof stock);
        std::memcpy(code + 2, &rel, 4);
        failure = Addr(code + 0x40);
        g_rsxr_resume_XI1 = reinterpret_cast<uintptr_t>(code + sizeof stock);
    }
};

// One hit with `body` as the loader's inflated buffer: the stream object is at ESP + 14h (+10h buffer, +18h size).
uint32_t Hit(Bytes& body) {
    uint32_t stack[16]{};
    RsxrMid::Context c{};

    stack[9] = Addr(body.data());
    stack[11] = static_cast<uint32_t>(body.size());
    c.esp = Addr(stack);
    c.jump = 0x1234;
    Rsxr_Mid_XI1(c);

    return c.jump;
}

// An empty six-string guitar chart: header, 14 empty sections, MetaData with six tuning entries, writer padding.
Bytes EmptyNative() {
    const uint8_t header[] = {'S', 'N', 'X', 'R', 1, 0, 6, 0};
    Bytes b(header, header + sizeof header);

    b.resize(b.size() + 14 * 4 + 79);
    b.push_back(6);
    b.resize(b.size() + 3 + 6 * 2 + 12 + 256);

    return b;
}
}  // namespace

TEST(native_hook_leaves_a_stock_body_alone) {
    const Site site;
    Bytes body(200, 0x5A);
    const Bytes before = body;

    CHECK(Hit(body) == 0x1234);
    CHECK(body == before);
}

TEST(native_hook_converts_in_place) {
    const Site site;
    Bytes body = EmptyNative();
    const Bytes native = body;

    CHECK(Hit(body) == 0x1234);
    CHECK(body.size() == native.size());
    CHECK(std::memcmp(body.data(), native.data() + 8, native.size() - 8 - 256) == 0);
    CHECK(std::memcmp(body.data(), "SNXR", 4) != 0);
}

TEST(native_hook_fails_the_load_of_a_bad_native_body) {
    const Site site;
    Bytes body = EmptyNative();

    body[9] = 9;  // nine strings: no such instrument
    const Bytes before = body;

    CHECK(Hit(body) == site.failure);
    CHECK(body == before);

    Bytes cut = EmptyNative();

    cut.resize(40);
    CHECK(Hit(cut) == site.failure);
}

namespace {
// One JM1 hit: the parser's cdecl frame at its entry (return address, output, 0, pointer to the text cursor).
const char* ParsedText(const char* text) {
    const char* cursor = text;
    uint32_t stack[4]{0, 0, 0, Addr(&cursor)};
    RsxrMid::Context c{};

    c.esp = Addr(stack);
    c.jump = 0x1234;
    Rsxr_Mid_JM1(c);
    CHECK(c.jump == 0x1234);

    return cursor;
}
}  // namespace

TEST(native_hook_json_without_native_arrangements_is_parsed_as_it_is) {
    const char stock[] = "{\"Tuning\": {\"string0\": 0, \"string1\": 0}}";

    CHECK(ParsedText(stock) == stock);
    CHECK(ParsedText("") != nullptr);
}

TEST(native_hook_json_with_a_native_arrangement_is_parsed_from_the_rewritten_text) {
    const char native[] = "{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": "
                          "{\"string0\": -2, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, "
                          "\"string5\": 5, \"string6\": 6}}";
    const char* parsed = ParsedText(native);

    CHECK(parsed != native);
    CHECK(std::strcmp(parsed, "{\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, "
                              "\"string4\": 5, \"string5\": 6, \"RsxrString6\": -2}}") == 0);
}

TEST(native_hook_package_magic_becomes_the_stock_one) {
    uint32_t frame[0x20]{};  // EBP - 70h is the header the loader copies into the table of contents
    RsxrMid::Context c{};

    c.ebp = Addr(frame) + 0x70;
    c.eax = 0x50535852;  // "PSXR"
    frame[0] = c.eax;
    Rsxr_Mid_PN1(c);
    CHECK(c.eax == 0x50534152 && frame[0] == 0x50534152);

    c.eax = 0x50534152;  // "PSAR": untouched
    frame[0] = 0x11111111;
    Rsxr_Mid_PN1(c);
    CHECK(c.eax == 0x50534152 && frame[0] == 0x11111111);

    c.eax = 0x52533751;  // anything else stays wrong, the stock compare rejects it
    Rsxr_Mid_PN1(c);
    CHECK(c.eax == 0x52533751 && frame[0] == 0x11111111);
}
