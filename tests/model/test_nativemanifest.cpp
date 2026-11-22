// The native manifest rewriter (model/nativemanifest.h): native Tuning objects become the current form.
#include "model/nativemanifest.h"
#include "test.h"
#include <string>

namespace {
using rsxrnative::JsonExtent;
using rsxrnative::ManifestStats;
using rsxrnative::RewriteManifest;

// An arrangement's attributes named `name` ("Bass" is a bass, any other name a guitar) with the marker and a
// native Tuning of the given values.
std::string Attributes(const char* name, std::initializer_list<int> tuning) {
    std::string s = "{\"ArrangementName\": \"";

    s += name;
    s += "\", \"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {";

    int i = 0;

    for (int v : tuning) {
        s += (i ? ", \"string" : "\"string") + std::to_string(i) + "\": " + std::to_string(v);
        ++i;
    }

    return s + "}, \"After\": 1}";
}

std::string Current(const char* tuning, const char* name = "Lead") {
    return std::string("{\"ArrangementName\": \"") + name + "\", \"Tuning\": {" + tuning + "}, \"After\": 1}";
}

// The rewritten text of `in`; "<unchanged>" when the rewriter leaves it to the game.
std::string Run(const std::string& in, ManifestStats* stats = nullptr) {
    std::string out = "untouched";

    if (!RewriteManifest(in.data(), in.size(), &out, stats)) {
        CHECK(out == "untouched");
        return "<unchanged>";
    }

    return out;
}

const char kSix[] = "\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0";
}  // namespace

TEST(nativemanifest_guitar_strings_move_to_their_slots) {
    ManifestStats stats;

    CHECK(Run(Attributes("Lead", {1, 2, 3, 4, 5, 6}), &stats) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6"));
    CHECK(stats.rewritten == 1 && stats.bad == 0);
    CHECK(Run(Attributes("Lead", {-7, 1, 2, 3, 4, 5, 6})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, "
                  "\"RsxrString6\": -7"));
    CHECK(Run(Attributes("Lead", {-8, -7, 1, 2, 3, 4, 5, 6})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, "
                  "\"RsxrString6\": -7, \"RsxrString7\": -8"));
}

TEST(nativemanifest_bass_strings_move_to_their_slots) {
    CHECK(Run(Attributes("Bass", {1, 2, 3, 4})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 0, \"string5\": 0",
                  "Bass"));
    CHECK(Run(Attributes("Bass", {-5, 1, 2, 3, 4})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": -5, \"string5\": 0, "
                  "\"RsxrString4\": -5",
                  "Bass"));
    CHECK(Run(Attributes("Bass", {-5, 1, 2, 3, 4, 9})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": -5, \"string5\": 9, "
                  "\"RsxrString4\": -5, \"RsxrString5\": 9",
                  "Bass"));
}

TEST(nativemanifest_the_arrangement_name_decides_the_instrument) {
    ManifestStats stats;
    const char seven[] = "\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, "
                         "\"string5\": 6, \"RsxrString6\": -7";

    // Any name but "Bass" is a guitar, as is an arrangement without a name.
    CHECK(Run(Attributes("Rhythm", {-7, 1, 2, 3, 4, 5, 6})) == Current(seven, "Rhythm"));
    CHECK(Run(Attributes("bass", {-7, 1, 2, 3, 4, 5, 6})) == Current(seven, "bass"));
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": -7, \"string1\": 1, \"string2\": 2, "
              "\"string3\": 3, \"string4\": 4, \"string5\": 5, \"string6\": 6}}") ==
          std::string("{\"Tuning\": {") + seven + "}}");

    // Five strings are a bass only.
    CHECK(Run(Attributes("Lead", {-5, 1, 2, 3, 4}), &stats) ==
          Current("\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 0"));
    CHECK(stats.rewritten == 0 && stats.bad == 1);

    // Other members of the marker are not read.
    CHECK(Run("{\"ArrangementName\": \"Bass\", \"ExtendedRange\": {\"Version\": 1, \"Instrument\": \"guitar\"}, "
              "\"Tuning\": {\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4}}") ==
          "{\"ArrangementName\": \"Bass\", \"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, "
          "\"string3\": 4, \"string4\": -5, \"string5\": 0, \"RsxrString4\": -5}}");
}

TEST(nativemanifest_marker_is_removed_wherever_it_stands) {
    const std::string marker = "\"ExtendedRange\": {\"Version\": 1}";
    const std::string native = "\"Tuning\": {\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, "
                               "\"string4\": 0, \"string5\": 0, \"string6\": 0}";
    const std::string current = std::string("\"Tuning\": {") + kSix + ", \"RsxrString6\": 0}";

    CHECK(Run("{" + marker + ",\r\n  " + native + "}") == "{" + current + "}");
    CHECK(Run("{" + native + " ,\r\n  " + marker + "\r\n}") == "{" + current + "\r\n}");
    CHECK(Run("{\"A\": [1, {\"b\": \"}\\\"\"}], " + marker + ", \"B\": 2, " + native + "}") ==
          "{\"A\": [1, {\"b\": \"}\\\"\"}], \"B\": 2, " + current + "}");
}

TEST(nativemanifest_every_arrangement_of_a_header_file) {
    ManifestStats stats;
    const std::string in = "{\"Entries\": {\"A\": {\"Attributes\": " + Attributes("Lead", {0, 0, 0, 0, 0, 0, 0}) +
                           "}, \"B\": {\"Attributes\": " + Attributes("Bass", {0, 0, 0, 0, 0}) +
                           "}, \"C\": {\"Attributes\": {\"Tuning\": {\"string0\": 3}}}}, \"InsertRoot\": \"x\"}";
    const std::string want =
        "{\"Entries\": {\"A\": {\"Attributes\": " + Current((std::string(kSix) + ", \"RsxrString6\": 0").c_str()) +
        "}, \"B\": {\"Attributes\": " + Current((std::string(kSix) + ", \"RsxrString4\": 0").c_str(), "Bass") +
        "}, \"C\": {\"Attributes\": {\"Tuning\": {\"string0\": 3}}}}, \"InsertRoot\": \"x\"}";

    CHECK(Run(in, &stats) == want);
    CHECK(stats.rewritten == 2 && stats.bad == 0);
}

TEST(nativemanifest_text_without_a_native_arrangement_is_left_alone) {
    CHECK(Run("{\"Tuning\": {\"string0\": 0, \"string6\": 1}}") == "<unchanged>");
    CHECK(Run("{\"Note\": \"\\\"ExtendedRange\\\"\", \"Tuning\": {\"string0\": 0}}") == "<unchanged>");
    CHECK(Run("") == "<unchanged>");
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 0}") == "<unchanged>");  // not closed
    CHECK(Run("{\"ExtendedRange\": 1, \"Tuning\": {\"string0\": 0}} trailing") == "<unchanged>");
    CHECK(Run("{\"ExtendedRange\": 1x\", \"Tuning\": {\"string0\": 0}, \"a\": \"}\"}") ==
          "<unchanged>");  // no quote in a literal

    std::string out;

    CHECK(!RewriteManifest(nullptr, 0, &out));
    CHECK(!RewriteManifest("{}", 2, nullptr));
}

TEST(nativemanifest_an_invalid_native_arrangement_gets_six_harmless_entries) {
    ManifestStats stats;
    const std::string six = Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, "
                                    "\"string5\": 6");

    // Nine guitar strings, five guitar strings, seven bass strings.
    CHECK(Run(Attributes("Lead", {1, 2, 3, 4, 5, 6, 7, 8, 9}), &stats) == six);
    CHECK(stats.rewritten == 0 && stats.bad == 1);
    CHECK(Run(Attributes("Lead", {1, 2, 3, 4, 5})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 0"));
    CHECK(Run(Attributes("Bass", {1, 2, 3, 4, 5, 6, 7})) ==
          Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6",
                  "Bass"));

    // An unknown version, a marker that is not an object, no Tuning object.
    const std::string native = "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, "
                               "\"string4\": 5, \"string5\": 6, \"string6\": 7}";
    const std::string tuned = "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, "
                              "\"string4\": 5, \"string5\": 6}";

    CHECK(Run("{\"ExtendedRange\": {\"Version\": 2}, " + native + "}") == "{" + tuned + "}");
    CHECK(Run("{\"ExtendedRange\": 1, " + native + "}") == "{" + tuned + "}");
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": 5}", &stats) == "{\"Tuning\": 5}");
    CHECK(stats.bad == 1);

    // Keys that are not exactly string0..stringN-1 with number values.
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, "
              "\"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"string9\": 7}}") ==
          "{" + tuned + "}");
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, "
              "\"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"string6\": \"x\"}}") ==
          "{" + tuned + "}");
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, "
              "\"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"string5\": 7}}") ==
          "{" + tuned + "}");
}

TEST(nativemanifest_extent_ends_at_the_closing_bracket) {
    const char text[] = "  {\"a\": [1, {\"b\": \"}]\\\"\"}], \"c\": {}}GARBAGE{";
    const size_t end = sizeof text - 1 - 8;

    CHECK(JsonExtent(text, sizeof text) == end);
    CHECK(JsonExtent(text, end) == end);
    CHECK(JsonExtent(text, end - 1) == 0);  // not closed within the limit
    CHECK(JsonExtent("[1, 2] x", 16) == 6);
    CHECK(JsonExtent(" \xEF\xBB\xBF{}", 16) == 0);  // a mark is only skipped at the very start
    CHECK(JsonExtent("\xEF\xBB\xBF\r\n{}", 16) == 7);
    CHECK(JsonExtent("{\"a\": 1", 16) == 0);  // ends at the terminator, never closed
    CHECK(JsonExtent("{\"a\\", 16) == 0);
    CHECK(JsonExtent("12", 16) == 0 && JsonExtent("", 16) == 0 && JsonExtent(nullptr, 16) == 0);
}

TEST(nativemanifest_text_with_a_mark_or_bytes_behind_it) {
    const std::string native = Attributes("Lead", {-7, 1, 2, 3, 4, 5, 6});
    const std::string current =
        Current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, "
                "\"string5\": 6, \"RsxrString6\": -7");
    const std::string marked = "\xEF\xBB\xBF" + native, behind = native + "}}junk";

    CHECK(Run(marked) == "\xEF\xBB\xBF" + current);
    CHECK(Run(behind) == "<unchanged>");  // the caller passes the value's own extent
    CHECK(JsonExtent(behind.c_str(), behind.size()) == native.size());

    const char two[2] = {'{', '}'};  // nothing behind it: the mark test must not read a third byte

    CHECK(JsonExtent(two, 1u << 20) == 2);
}

TEST(nativemanifest_offsets_are_whole_semitones_within_two_octaves) {
    ManifestStats stats;
    const std::string marker = "{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": ";
    const std::string rest = ", \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 5}}";
    const std::string zeroed = "{\"Tuning\": {\"string0\": 0" + rest;

    CHECK(Run(Attributes("Lead", {-24, 24, 0, 0, 0, 0}), &stats) ==
          Current("\"string0\": -24, \"string1\": 24, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0"));
    CHECK(stats.rewritten == 1 && stats.bad == 0);

    for (const char* value : {"25", "-25", "0.5", "1e1", "07", "100", "-", "true"}) {
        CHECK(Run(marker + value + rest, &stats) == zeroed);
        CHECK(stats.rewritten == 0 && stats.bad == 1);
    }
}

TEST(nativemanifest_repeated_and_nested_members_never_leave_a_seventh_key) {
    ManifestStats stats;
    const std::string marker = "\"ExtendedRange\": {\"Version\": 1}";
    const std::string native = "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, "
                               "\"string4\": 5, \"string5\": 6, \"string6\": 7}";
    const std::string tuned = "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, "
                              "\"string4\": 5, \"string5\": 6}";

    // A repeated Tuning: the game's parser may keep either, so both become six entries.
    CHECK(Run("{" + marker + ", \"Tuning\": {}, " + native + "}", &stats) ==
          std::string("{\"Tuning\": {") + kSix + "}, " + tuned + "}");
    CHECK(stats.rewritten == 0 && stats.bad == 1);
    CHECK(Run("{" + marker + ", " + native + ", \"Tuning\": 5}") == "{" + tuned + ", \"Tuning\": 5}");

    // A marker inside a marker: the outer arrangement is still rewritten.
    CHECK(Run("{\"ExtendedRange\": {\"Version\": 1, \"ExtendedRange\": 1}, " + native + "}") ==
          "{\"Tuning\": {\"string0\": 2, \"string1\": 3, \"string2\": 4, \"string3\": 5, \"string4\": 6, "
          "\"string5\": 7, \"RsxrString6\": 1}}");

    // A marked arrangement inside another one's Tuning: the outer Tuning replaces it.
    CHECK(Run("{" + marker + ", \"Tuning\": {\"string0\": {" + marker + ", " + native + "}, \"string6\": 1}}") ==
          std::string("{\"Tuning\": {") + kSix + "}}");
}
