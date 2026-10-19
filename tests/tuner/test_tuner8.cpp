#include "config_fixture.h"
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/tuner.h"
#include "hook_rules.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {
template <class T>
void Put(uint8_t* b, int off, T v) {
    std::memcpy(b + off, &v, sizeof v);
}

struct Objects {
    uint8_t session[0x278]{}, action[0x3B0]{}, guitar[0x38]{}, owner[0x38]{};
    uint8_t strings[rs7gs::kGsSize]{}, info[rs7gs::kInfoSize]{};

    Objects() {
        Put(session, 0x2C, guitar);
        Put(action, 0x37C, guitar);
        Put(guitar, 0xC, strings);
        Put(guitar, 0x24, info);
        Put(strings, 0, owner);
        Put(owner, 0x24, info);
        Put(strings, rs7gs::kInfo, info);
        Put(strings, rs7gs::kGuitar, guitar);
        Put(strings, rs7gs::kMarkerOff, rs7gs::kMarker);
        Put(strings, rs7gs::kMagicOff, rs7gs::kMagic);
        Put(info, rs7gs::kInfoMagicOff, rs7gs::kInfoMagic);
        info[rs7gs::kInfoActive] = 7;
        info[rs7gs::kInfoLoaded] = 7;

        for (int s = 0; s < 8; ++s) {
            Put(strings, rs7gs::kMain + 4 * s, guitar);
        }
    }
};

struct Switches {
    uintptr_t tuner = g_rs7_native_tuner, strings = g_rs7_native_strings, fretgrid = g_rs7_native_fretgrid,
              colours = g_rs7_native_colours, row = g_rs7_fg_row;

    Switches() { g_rs7_fg_row = rs7fg::kRowNative; }  // the deferred FretGrid layout is written (fix/fretgrid)

    ~Switches() {
        g_rs7_native_tuner = tuner;
        g_rs7_native_strings = strings;
        g_rs7_native_fretgrid = fretgrid;
        g_rs7_native_colours = colours;
        g_rs7_fg_row = row;
    }
};
}  // namespace

TEST(tuner8_copy_dispatch_requires_switch_native_owner_and_valid_count) {
    Switches save;
    Objects o;

    g_rs7_native_fretgrid = 1;
    g_rs7_native_tuner = 0;
    g_rs7_native_strings = 1;
    CHECK(!Rs7Tuner8_Use(0, o.session));
    g_rs7_native_tuner = 1;
    g_rs7_native_strings = 0;
    CHECK(!Rs7Tuner8_Use(0, o.session));
    g_rs7_native_strings = 1;
    CHECK(Rs7Tuner8_Use(0, o.session));
    CHECK(Rs7Tuner8_Use(1, o.session));
    CHECK(Rs7Tuner8_Use(2, o.action));
    CHECK(!Rs7Tuner8_Use(3, o.session));
    CHECK(!Rs7Tuner8_Use(0, nullptr));

    for (int n : {4, 6, 7, 8}) {
        o.info[rs7gs::kInfoActive] = uint8_t(n);
        o.info[rs7gs::kInfoLoaded] = uint8_t(n);
        CHECK(Rs7Tuner8_Use(0, o.session));
    }

    for (int n : {0, 9, 255}) {
        o.info[rs7gs::kInfoActive] = uint8_t(n);
        CHECK(!Rs7Tuner8_Use(0, o.session));
    }

    o.info[rs7gs::kInfoActive] = 8;
    o.info[rs7gs::kInfoLoaded] = 7;
    CHECK(!Rs7Tuner8_Use(0, o.session));
    o.info[rs7gs::kInfoActive] = 7;
    Put(o.info, rs7gs::kInfoMagicOff, uint32_t(0));
    CHECK(!Rs7Tuner8_Use(0, o.session));
    Put(o.info, rs7gs::kInfoMagicOff, rs7gs::kInfoMagic);
    Put(o.strings, rs7gs::kMain + 6 * 4, static_cast<uint8_t*>(nullptr));
    CHECK(!Rs7Tuner8_Use(0, o.session));
    Put(o.strings, rs7gs::kMain + 6 * 4, o.guitar);
    Put(o.strings, rs7gs::kInfo, o.owner);
    CHECK(!Rs7Tuner8_Use(0, o.session));
    Put(o.strings, rs7gs::kInfo, o.info);
    Put(o.strings, rs7gs::kMarkerOff, uint16_t(0));
    CHECK(!Rs7Tuner8_Use(0, o.session));
}

TEST(tuner8_sites_are_on_by_default_and_require_native_strings) {
    Switches save;

    g_rs7_native_fretgrid = 1;
    g_rs7_native_colours = 1;

    for (const char* id : {"TU8H", "TU8C", "TU8A", "TU8ALLOC", "TU8INIT", "TU8FREE", "TS8SEL", "TS8MD2", "TS8FIN",
                           "TS8PKL", "TS8F1", "TS8F2", "TS8F3", "TS8RS", "TS8DT", "TS8WR", "TS8SN1", "TS8SN2"}) {
        const auto* h = Rs7_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        g_rs7_native_tuner = 0;
        CHECK(!Rs7_ShouldPatch(*h, true, false, true, true));
        g_rs7_native_tuner = 1;
        CHECK(!Rs7_ShouldPatch(*h, true, false, true, false));
        CHECK(Rs7_ShouldPatch(*h, false, false, true, true));
        g_rs7_native_fretgrid = 0;
        CHECK(!Rs7_ShouldPatch(*h, false, false, true, true));
        g_rs7_native_fretgrid = 1;
        g_rs7_native_colours = 0;
        CHECK(!Rs7_ShouldPatch(*h, false, false, true, true));
        g_rs7_native_colours = 1;
    }
}

TEST(tuner8_removed_ini_switch_is_ignored) {
    // 1.0: [neck] native_tuner is gone; an old ini that still has it loads and only logs it.
    wchar_t dir[MAX_PATH]{}, path[MAX_PATH]{};

    GetTempPathW(MAX_PATH, dir);
    CHECK(GetTempFileNameW(dir, L"tu8", 0, path) != 0);
    FILE* file = nullptr;

    _wfopen_s(&file, path, L"wb");
    CHECK(file != nullptr);

    if (file) {
        std::fprintf(file, "[neck]\r\nnative_tuner=0\r\n");
        std::fclose(file);
        CHECK_EQ(Rs7Test_LoadConfig(path).removedKeys, 1);
    }

    DeleteFileW(path);
}

TEST(tuner8_session_tail_keeps_stock_prefix_and_starts_unused) {
    Switches save;

    g_rs7_native_tuner = 1;
    uint8_t session[rs7tu::kSessionSize]{};

    std::memset(session, 0xA5, sizeof session);

    for (unsigned i = 0; i < rs7tu::kStockSize; ++i) {
        session[i] = uint8_t(i * 13 + 7);
    }

    uint8_t prefix[rs7tu::kStockSize];

    std::memcpy(prefix, session, sizeof prefix);
    CHECK(!Rs7Tuner8_HasSession(session));
    Rs7Tuner8_InitSession(session);
    CHECK(Rs7Tuner8_HasSession(session));
    CHECK(std::memcmp(session, prefix, sizeof prefix) == 0);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(session + rs7tu::kMagicOff), rs7tu::kMagic);

    for (int k = 0; k < 8; ++k) {
        CHECK_EQ(reinterpret_cast<int32_t*>(session + rs7tu::kLastPick)[k], -2);
    }

    CHECK_EQ(session[rs7tu::kNative], 0);
    CHECK_EQ(session[rs7tu::kCount], 0);
    CHECK(!Rs7Tuner8_IsNative(session));  // a grown session runs the game's code until TS
    CHECK_EQ(Rs7Tuner8_FreeSessionSize(session), rs7tu::kSessionSize);
    CHECK(!Rs7Tuner8_HasSession(session));
    CHECK_EQ(Rs7Tuner8_FreeSessionSize(session), rs7tu::kStockSize);
}

TEST(tuner8_session_storage_switch_off_does_not_write_or_register) {
    Switches save;

    g_rs7_native_tuner = 0;
    uint8_t session[rs7tu::kSessionSize]{};

    std::memset(session, 0xA5, sizeof session);
    Rs7Tuner8_InitSession(session);
    CHECK(!Rs7Tuner8_HasSession(session));

    for (uint8_t b : session) {
        CHECK_EQ(b, 0xA5);
    }

    CHECK_EQ(Rs7Tuner8_FreeSessionSize(session), rs7tu::kStockSize);
}

TEST(tuner8_order_is_lowest_string_first) {
    uint8_t o[8];

    rs7tu::Order(6, o);

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(o[i], i);
    }

    rs7tu::Order(4, o);

    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(o[i], i);
    }

    rs7tu::Order(7, o);
    const uint8_t seven[] = {6, 0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(o[i], seven[i]);
    }

    rs7tu::Order(8, o);
    const uint8_t eight[] = {7, 6, 0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(o[i], eight[i]);
    }

    // Steps: identity for six (stock cur+1 / cur-1 inside 0..n-1), the order for seven / eight.
    rs7tu::Order(6, o);

    for (int c = 0; c < 6; ++c) {
        CHECK_EQ(rs7tu::Step(o, 6, c, 1), c < 5 ? c + 1 : -1);
        CHECK_EQ(rs7tu::Step(o, 6, c, -1), c > 0 ? c - 1 : -1);
    }

    CHECK_EQ(rs7tu::Step(o, 6, 6, 1), -1);
    rs7tu::Order(7, o);
    CHECK_EQ(rs7tu::Step(o, 7, 6, 1), 0);
    CHECK_EQ(rs7tu::Step(o, 7, 0, -1), 6);
    CHECK_EQ(rs7tu::Step(o, 7, 6, -1), -1);
    CHECK_EQ(rs7tu::Step(o, 7, 5, 1), -1);
    rs7tu::Order(8, o);
    CHECK_EQ(rs7tu::Step(o, 8, 7, 1), 6);
    CHECK_EQ(rs7tu::Step(o, 8, 6, 1), 0);
    CHECK_EQ(rs7tu::Step(o, 8, 6, -1), 7);
}

TEST(tuner8_targets_and_labels) {
    const int offs[6] = {-2, -2, -2, -2, -2, -2}, extra[2] = {-2, 1};
    int open[8], off[8];

    rs7tu::Targets(offs, extra, open, off);
    const int want[8] = {38, 43, 48, 53, 57, 62, 33, 31};

    for (int s = 0; s < 8; ++s) {
        CHECK_EQ(open[s], want[s]);
        CHECK_EQ(off[s], s < 6 ? -2 : extra[s - 6]);
    }

    CHECK(!std::strcmp(rs7tu::NoteName(35), "B"));
    CHECK(!std::strcmp(rs7tu::NoteName(30), "F#"));
    CHECK(!std::strcmp(rs7tu::NoteName(-1), "B"));
}

namespace {
struct Session {
    uint8_t b[rs7tu::kSessionSize]{};

    Session() {
        g_rs7_native_tuner = 1;

        for (int s = 0; s < 6; ++s) {
            reinterpret_cast<int32_t*>(b + rs7tu::kStockOpen)[s] = 40 + 5 * s;
            reinterpret_cast<uint32_t*>(b + rs7tu::kStockLetters)[2 * s] = 0x1000 + s;
            reinterpret_cast<uint32_t*>(b + rs7tu::kStockLetters)[2 * s + 1] = 0x2000 + s;
            reinterpret_cast<uint32_t*>(b + rs7tu::kStockPicks)[s] = 0x3000 + s;
            reinterpret_cast<int32_t*>(b + rs7tu::kStockLast)[s] = s;
        }

        Rs7Tuner8_InitSession(b);
    }

    ~Session() { Rs7Tuner8_FreeSessionSize(b); }

    template <class T>
    T& at(uint32_t off) {
        return *reinterpret_cast<T*>(b + off);
    }
};
}  // namespace

TEST(tuner8_apply_makes_a_native_session_and_moves_the_entities) {
    Switches save;
    Session s;
    const int offs[6] = {0, 0, 0, 0, 0, 0}, extra[2] = {0, 0};

    CHECK(!Rs7Tuner8_Apply(s.b, offs, 6, extra));  // six strings: the game's code
    CHECK(!Rs7Tuner8_IsNative(s.b));
    CHECK(Rs7Tuner8_Apply(s.b, offs, 7, extra));
    CHECK(Rs7Tuner8_IsNative(s.b));
    CHECK_EQ(s.at<uint8_t>(rs7tu::kCount), 7);
    CHECK_EQ(s.at<uint8_t>(rs7tu::kOrder), 6);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rs7tu::kLetters + 8 * k), 0x1000u + k);
        CHECK_EQ(s.at<uint32_t>(rs7tu::kStockLetters + 8 * k), 0u);  // moved, not copied
        CHECK_EQ(s.at<uint32_t>(rs7tu::kPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rs7tu::kStockPicks + 4 * k), 0u);
        CHECK_EQ(s.at<int32_t>(rs7tu::kLastPick + 4 * k), k);
    }

    CHECK_EQ(s.at<int32_t>(rs7tu::kOpen + 4 * 6), 35);
    CHECK_EQ(s.at<int32_t>(rs7tu::kLastPick + 4 * 6), -2);
    CHECK_EQ(s.at<uint32_t>(rs7tu::kPicks + 4 * 6), 0u);
    // Re-applied: 8 strings, then 6 (a native session stays native, its count and order follow).
    const int extra8[2] = {-1, 2};

    CHECK(Rs7Tuner8_Apply(s.b, offs, 8, extra8));
    CHECK_EQ(s.at<uint8_t>(rs7tu::kCount), 8);
    CHECK_EQ(s.at<uint8_t>(rs7tu::kOrder), 7);
    CHECK_EQ(s.at<int32_t>(rs7tu::kOpen + 4 * 6), 34);
    CHECK_EQ(s.at<int32_t>(rs7tu::kOpen + 4 * 7), 32);
    CHECK_EQ(s.at<uint32_t>(rs7tu::kLetters), 0x1000u);  // entities stay in the tail
    CHECK(Rs7Tuner8_Apply(s.b, offs, 6, extra));
    CHECK_EQ(s.at<uint8_t>(rs7tu::kCount), 6);
    CHECK_EQ(s.at<uint8_t>(rs7tu::kOrder), 0);
    CHECK(Rs7Tuner8_IsNative(s.b));
    g_rs7_native_tuner = 0;
    CHECK(!Rs7Tuner8_IsNative(s.b));
    CHECK(!Rs7Tuner8_Apply(s.b, offs, 7, extra));
    g_rs7_native_tuner = 1;
}

TEST(tuner8_goes_native_for_a_seven_built_guitar_whatever_the_target) {
    // fix/menutuner (a): pure rule.
    CHECK(!rs7tu::GoesNative(6, false, 6));
    CHECK(!rs7tu::GoesNative(6, false, 0));  // no guitar bound yet
    CHECK(!rs7tu::GoesNative(4, false, 4));  // bass
    CHECK(rs7tu::GoesNative(6, false, 7));
    CHECK(rs7tu::GoesNative(6, false, 8));
    CHECK(rs7tu::GoesNative(7, false, 6));
    CHECK(rs7tu::GoesNative(6, true, 6));
}

TEST(tuner8_six_string_target_on_a_seven_built_guitar_is_a_native_session) {
    // The owner's crash (tests/owner-crash1): main-menu tuner, 6-string tuning, guitar built with 7 -> the stock
    // UI_PlayerTuner_SetupPicks 0x5B1E50 wrote +144h[6]. Now TS makes the session native (six strings, 8-slot tail).
    Switches save;
    Session s;
    Objects o;

    g_rs7_native_strings = 1;
    std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);  // the session's guitar = o.guitar (native GuitarStrings)
    const int offs[6] = {0, 0, 0, 0, 0, 0}, extra[2] = {0, 0};

    o.info[0xB] = 6;
    CHECK_EQ(Rs7Tuner8_GuitarCount(s.b), 6u);
    CHECK(!Rs7Tuner8_Apply(s.b, offs, 6, extra));  // 6 on 6: the game's code, as before
    CHECK(!Rs7Tuner8_IsNative(s.b));
    o.info[0xB] = 7;
    CHECK_EQ(Rs7Tuner8_GuitarCount(s.b), 7u);
    CHECK(Rs7Tuner8_Apply(s.b, offs, 6, extra));
    CHECK(Rs7Tuner8_IsNative(s.b));
    CHECK_EQ(s.at<uint8_t>(rs7tu::kCount), 6);  // six pegs / strings tuned
    CHECK_EQ(s.at<uint8_t>(rs7tu::kOrder), 0);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rs7tu::kPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rs7tu::kStockPicks + 4 * k), 0u);
    }

    CHECK_EQ(s.at<uint32_t>(rs7tu::kPicks + 4 * 6), 0u);  // slot 6 exists in the tail (the crash slot)
}

TEST(tuner8_ensure_native_converts_only_stock_sessions_on_seven_built_guitars) {
    Switches save;
    Objects o;

    g_rs7_native_strings = 1;

    {  // guitar built with 6 (and bass, and no guitar): stays stock
        Session s;

        std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);
        o.info[0xB] = 6;
        CHECK(!Rs7Tuner8_EnsureNative(s.b, "test"));
        CHECK(!Rs7Tuner8_IsNative(s.b));
        o.info[0xB] = 4;
        CHECK(!Rs7Tuner8_EnsureNative(s.b, "test"));
        CHECK(!Rs7Tuner8_IsNative(s.b));
        std::memset(s.b + 0x2C, 0, 4);
        CHECK(!Rs7Tuner8_EnsureNative(s.b, "test"));
        CHECK_EQ(Rs7Tuner8_GuitarCount(s.b), 0u);
    }

    {  // guitar built with 7: native, six strings, the session's own offsets (+50h)
        Session s;

        std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);

        for (int k = 0; k < 6; ++k) {
            s.at<int32_t>(rs7tu::kStockOff + 4 * k) = -2;
        }

        o.info[0xB] = 7;
        CHECK(Rs7Tuner8_EnsureNative(s.b, "test"));
        CHECK(Rs7Tuner8_IsNative(s.b));
        CHECK_EQ(s.at<uint8_t>(rs7tu::kCount), 6);

        for (int k = 0; k < 6; ++k) {
            CHECK_EQ(s.at<int32_t>(rs7tu::kOffsets + 4 * k), -2);
        }

        CHECK_EQ(s.at<int32_t>(rs7tu::kOpen), 38);
        CHECK(Rs7Tuner8_EnsureNative(s.b, "test"));  // idempotent
    }

    {  // the guitar has no native GuitarStrings: cannot be made native (logged), stays stock
        Session s;
        Objects bad;

        std::memcpy(s.b + 0x2C, bad.session + 0x2C, 4);
        bad.info[0xB] = 7;
        Put(bad.strings, rs7gs::kMagicOff, uint32_t(0));
        CHECK(!Rs7Tuner8_EnsureNative(s.b, "test"));
        CHECK(!Rs7Tuner8_IsNative(s.b));
    }

    {  // an unregistered (stock-sized) session: never touched
        uint8_t stock[rs7tu::kSessionSize]{};

        std::memcpy(stock + 0x2C, o.session + 0x2C, 4);
        o.info[0xB] = 7;
        CHECK(!Rs7Tuner8_EnsureNative(stock, "test"));
        CHECK_EQ(stock[rs7tu::kNative], 0);
    }
}

TEST(tuner8_apply_needs_a_registered_grown_session) {
    Switches save;

    g_rs7_native_tuner = 1;
    uint8_t stock[rs7tu::kSessionSize]{};
    const int offs[6]{}, extra[2]{};

    CHECK(!Rs7Tuner8_Apply(stock, offs, 7, extra));
    CHECK_EQ(stock[rs7tu::kNative], 0);
}

TEST(tuner8_mid_hooks_follow_the_order_and_leave_stock_sessions_alone) {
    Switches save;
    Session s;
    Rs7Mid::Context c{};

    c.ebx = uint32_t(uintptr_t(s.b));
    c.esi = uint32_t(uintptr_t(s.b));
    c.ecx = uint32_t(uintptr_t(s.b));
    c.jump = 0x1234;
    Rs7_Mid_TS8F1(c);
    Rs7_Mid_TS8F2(c);
    Rs7_Mid_TS8F3(c);  // stock session: replay only
    CHECK_EQ(c.jump, 0x1234u);
    CHECK_EQ(c.edi, 0u);
    CHECK_EQ(s.at<int32_t>(rs7tu::kCur), 0);
    const int offs[6]{}, extra[2]{};

    CHECK(Rs7Tuner8_Apply(s.b, offs, 7, extra));
    Rs7_Mid_TS8F1(c);
    CHECK_EQ(s.at<int32_t>(rs7tu::kCur), 6);
    CHECK_EQ(c.jump, 0x5AFB1Eu);
    CHECK_EQ(c.esi, c.ebx);
    s.at<int32_t>(rs7tu::kCur) = 3;
    c.jump = 0x1234;
    Rs7_Mid_TS8F2(c);
    CHECK_EQ(s.at<int32_t>(rs7tu::kCur), 6);
    CHECK_EQ(c.jump, 0x1234u);
    Rs7_Mid_TS8F3(c);
    CHECK_EQ(c.edi, 6u);
    c.edi = 5;
    s.at<uint8_t>(0x22D) = 1;  // reverse pass: the stock 5 stays
    Rs7_Mid_TS8F3(c);
    CHECK_EQ(c.edi, 5u);
    // Reset: the tail takes the standard tuning, zero peaks.
    s.at<int32_t>(rs7tu::kOpen + 4 * 6) = 99;
    s.at<float>(rs7tu::kPeaks) = 1.0f;
    Rs7_Mid_TS8RS(c);
    CHECK_EQ(s.at<int32_t>(rs7tu::kOpen + 4 * 6), 35);
    CHECK_EQ(s.at<int32_t>(rs7tu::kOpen), 40);
    CHECK(s.at<float>(rs7tu::kPeaks) == 0.0f);
    // Wrong instrument: the target of the string at [ebp+8].
    uint32_t frame[4]{0, 0, 6, 0};

    c.ebp = uint32_t(uintptr_t(frame));
    c.jump = 0;
    Rs7_Mid_TS8WR(c);
    CHECK_EQ(c.ecx, 6u);
    CHECK_EQ(c.edx, 35u);
    CHECK_EQ(c.jump, 0x5B2D8Fu);
    // Tuned sound: the note of string 6 itself (EDI is clamped to 5 by then).
    uint32_t stack[2]{0, uint32_t(uintptr_t(s.b))};

    c.esp = uint32_t(uintptr_t(stack));
    c.ecx = 6;
    Rs7_Mid_TS8SN1(c);
    uint32_t local[16]{};

    local[16 - 9] = uint32_t(uintptr_t(s.b));  // [ebp-24h]
    c.ebp = uint32_t(uintptr_t(local + 16));
    c.jump = 0;
    c.eax = 0;
    Rs7_Mid_TS8SN2(c);
    CHECK_EQ(c.eax, 35u);
    CHECK_EQ(c.jump, 0x5B2EAEu);
}

TEST(tuner8_dtor_hands_back_strings_0_to_5) {
    Switches save;
    Session s;
    const int offs[6]{}, extra[2]{};

    CHECK(Rs7Tuner8_Apply(s.b, offs, 7, extra));
    Rs7Mid::Context c{};

    c.ecx = uint32_t(uintptr_t(s.b));
    Rs7_Mid_TS8DT(c);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rs7tu::kStockLetters + 8 * k), 0x1000u + k);
        CHECK_EQ(s.at<uint32_t>(rs7tu::kStockPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rs7tu::kPicks + 4 * k), 0u);
    }

    CHECK(!Rs7Tuner8_IsNative(s.b));
}

TEST(tuner8_shared_tuner_sites_stay_and_the_b_phase_is_gone) {
    Switches save;

    g_rs7_native_fretgrid = 1;
    g_rs7_native_colours = 1;

    // 1.0: the six-slot B-phase tuner sites were removed with their stubs.
    for (const char* id :
         {"T1", "TA", "TR", "M1", "M2", "L1", "L2", "TLC", "TLI", "TLO1", "TLO2", "TF", "PK", "PE", "TD"}) {
        CHECK(Rs7_FindHook(id) == nullptr);
    }

    for (const char* id : {"TK", "TS", "W", "AT"}) {
        const auto* h = Rs7_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        g_rs7_native_tuner = 1;
        CHECK(Rs7_ShouldPatch(*h, true, false, true, true));
    }
}

TEST(tuner8_sessions_stay_stock_until_the_fretgrid_layout_is_written) {
    Switches save;
    Session s;
    const int offs[6]{}, extra[2]{};

    g_rs7_fg_row = rs7fg::kRowStock;  // TS8PKS / TS8PKP carry the native row stride
    CHECK(!Rs7Tuner8_Apply(s.b, offs, 7, extra));
    CHECK(!Rs7Tuner8_IsNative(s.b));
    g_rs7_fg_row = rs7fg::kRowNative;
    CHECK(Rs7Tuner8_Apply(s.b, offs, 7, extra));
}
