#include "config_fixture.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "hook_rules.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

extern "C" void __cdecl Rsxr_Mid_TS8SEL(RsxrMid::Context& c);

namespace {
template <class T>
void Put(uint8_t* b, int off, T v) {
    std::memcpy(b + off, &v, sizeof v);
}

struct Objects {
    uint8_t session[0x278]{}, action[0x3B0]{}, guitar[0x38]{}, owner[0x38]{};
    uint8_t strings[rsxrgs::kGsSize]{}, info[rsxrgs::kInfoSize]{};

    Objects() {
        Put(session, 0x2C, guitar);
        Put(action, 0x37C, guitar);
        Put(guitar, 0xC, strings);
        Put(guitar, 0x24, info);
        Put(strings, 0, owner);
        Put(owner, 0x24, info);
        Put(strings, rsxrgs::kInfo, info);
        Put(strings, rsxrgs::kGuitar, guitar);
        Put(strings, rsxrgs::kMarkerOff, rsxrgs::kMarker);
        Put(strings, rsxrgs::kMagicOff, rsxrgs::kMagic);
        Put(info, rsxrgs::kInfoMagicOff, rsxrgs::kInfoMagic);
        info[rsxrgs::kInfoActive] = 7;
        info[rsxrgs::kInfoLoaded] = 7;

        for (int s = 0; s < 8; ++s) {
            Put(strings, rsxrgs::kMain + 4 * s, guitar);
        }
    }
};

struct Switches {
    uintptr_t tuner = g_rsxr_native_tuner, strings = g_rsxr_native_strings, fretgrid = g_rsxr_native_fretgrid,
              colours = g_rsxr_native_colours, row = g_rsxr_fg_row;

    Switches() { g_rsxr_fg_row = rsxrfg::kRowNative; }  // the deferred FretGrid layout is written (fix/fretgrid)

    ~Switches() {
        g_rsxr_native_tuner = tuner;
        g_rsxr_native_strings = strings;
        g_rsxr_native_fretgrid = fretgrid;
        g_rsxr_native_colours = colours;
        g_rsxr_fg_row = row;
    }
};
}  // namespace

TEST(tuner8_copy_dispatch_follows_native_owner_and_valid_count) {
    Switches save;
    Objects o;

    g_rsxr_native_fretgrid = 1;
    g_rsxr_native_tuner = 0;
    g_rsxr_native_strings = 1;
    CHECK(RsxrTuner8_Use(0, o.session));
    g_rsxr_native_tuner = 1;
    g_rsxr_native_strings = 0;
    CHECK(RsxrTuner8_Use(0, o.session));
    g_rsxr_native_strings = 1;
    CHECK(RsxrTuner8_Use(0, o.session));
    CHECK(RsxrTuner8_Use(1, o.session));
    CHECK(RsxrTuner8_Use(2, o.action));
    CHECK(!RsxrTuner8_Use(3, o.session));
    CHECK(!RsxrTuner8_Use(0, nullptr));

    for (int n : {4, 6, 7, 8}) {
        o.info[rsxrgs::kInfoActive] = uint8_t(n);
        o.info[rsxrgs::kInfoLoaded] = uint8_t(n);
        CHECK(RsxrTuner8_Use(0, o.session));
    }

    for (int n : {0, 9, 255}) {
        o.info[rsxrgs::kInfoActive] = uint8_t(n);
        CHECK(!RsxrTuner8_Use(0, o.session));
    }

    o.info[rsxrgs::kInfoActive] = 8;
    o.info[rsxrgs::kInfoLoaded] = 7;
    CHECK(!RsxrTuner8_Use(0, o.session));
    o.info[rsxrgs::kInfoActive] = 7;
    Put(o.info, rsxrgs::kInfoMagicOff, uint32_t(0));
    CHECK(!RsxrTuner8_Use(0, o.session));
    Put(o.info, rsxrgs::kInfoMagicOff, rsxrgs::kInfoMagic);
    Put(o.strings, rsxrgs::kMain + 6 * 4, static_cast<uint8_t*>(nullptr));
    CHECK(!RsxrTuner8_Use(0, o.session));
    Put(o.strings, rsxrgs::kMain + 6 * 4, o.guitar);
    Put(o.strings, rsxrgs::kInfo, o.owner);
    CHECK(!RsxrTuner8_Use(0, o.session));
    Put(o.strings, rsxrgs::kInfo, o.info);
    Put(o.strings, rsxrgs::kMarkerOff, uint16_t(0));
    CHECK(!RsxrTuner8_Use(0, o.session));
}

TEST(tuner8_sites_are_on_by_default_and_require_native_strings) {
    Switches save;

    g_rsxr_native_fretgrid = 1;
    g_rsxr_native_colours = 1;

    for (const char* id : {"TU8H", "TU8C", "TU8A", "TU8ALLOC", "TU8INIT", "TU8FREE", "TS8SEL", "TS8MD2", "TS8FIN",
                           "TS8PKL", "TS8F1", "TS8F2", "TS8F3", "TS8RS", "TS8DT", "TS8WR", "TS8SN1", "TS8SN2"}) {
        const auto* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        g_rsxr_native_tuner = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true, false, true, true));
        g_rsxr_native_tuner = 1;
        CHECK(!Rsxr_ShouldPatch(*h, true, false, true, false));
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true));
        g_rsxr_native_fretgrid = 0;
        CHECK(!Rsxr_ShouldPatch(*h, false, false, true, true));
        g_rsxr_native_fretgrid = 1;
        g_rsxr_native_colours = 0;
        CHECK(!Rsxr_ShouldPatch(*h, false, false, true, true));
        g_rsxr_native_colours = 1;
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
        CHECK_EQ(RsxrTest_LoadConfig(path).removedKeys, 1);
    }

    DeleteFileW(path);
}

TEST(tuner8_session_tail_keeps_stock_prefix_and_starts_unused) {
    Switches save;

    g_rsxr_native_tuner = 1;
    uint8_t session[rsxrtu::kSessionSize]{};

    std::memset(session, 0xA5, sizeof session);

    for (unsigned i = 0; i < rsxrtu::kStockSize; ++i) {
        session[i] = uint8_t(i * 13 + 7);
    }

    uint8_t prefix[rsxrtu::kStockSize];

    std::memcpy(prefix, session, sizeof prefix);
    CHECK(!RsxrTuner8_HasSession(session));
    RsxrTuner8_InitSession(session);
    CHECK(RsxrTuner8_HasSession(session));
    CHECK(std::memcmp(session, prefix, sizeof prefix) == 0);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(session + rsxrtu::kMagicOff), rsxrtu::kMagic);

    for (int k = 0; k < 8; ++k) {
        CHECK_EQ(reinterpret_cast<int32_t*>(session + rsxrtu::kLastPick)[k], -2);
    }

    CHECK_EQ(session[rsxrtu::kNative], 0);
    CHECK_EQ(session[rsxrtu::kCount], 0);
    CHECK(!RsxrTuner8_IsNative(session));  // a grown session runs the game's code until TS
    CHECK_EQ(RsxrTuner8_FreeSessionSize(session), rsxrtu::kSessionSize);
    CHECK(!RsxrTuner8_HasSession(session));
    CHECK_EQ(RsxrTuner8_FreeSessionSize(session), rsxrtu::kStockSize);
}

TEST(tuner8_session_storage_survives_runtime_isolation) {
    Switches save;
    rsxrtest::Restore restoreClite(g_rsxr_clite);
    rsxrtest::Restore restorePorts(g_rsxr_publish_ports);
    rsxrtest::Restore restorePublish(g_rsxr_native_publish);

    Rsxr_ResetFeatureFailures();
    g_rsxr_native_tuner = g_rsxr_native_strings = g_rsxr_native_fretgrid = g_rsxr_native_colours = 1;
    uint8_t session[rsxrtu::kSessionSize]{};
    const int offs[6] = {}, extra[2] = {};

    Put(session, rsxrtu::kStockLetters, uint32_t(0x1000));
    Put(session, rsxrtu::kStockPicks, uint32_t(0x2000));
    RsxrTuner8_InitSession(session);
    CHECK(RsxrTuner8_Apply(session, offs, 7, extra));
    CHECK_EQ(*reinterpret_cast<uint32_t*>(session + rsxrtu::kStockLetters), 0u);
    CHECK(Rsxr_DisableFeature(RsxrFeature::Fretgrid));
    CHECK_EQ(g_rsxr_native_tuner, uintptr_t(0));
    CHECK(RsxrTuner8_IsNative(session));
    CHECK(RsxrTuner8_EnsureNative(session, "test"));
    CHECK(RsxrTuner8_Apply(session, offs, 6, extra));
    RsxrMid::Context c{};

    c.ecx = uint32_t(uintptr_t(session));
    Rsxr_Mid_TS8DT(c);
    CHECK(!RsxrTuner8_IsNative(session));
    CHECK_EQ(*reinterpret_cast<uint32_t*>(session + rsxrtu::kStockLetters), 0x1000u);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(session + rsxrtu::kStockPicks), 0x2000u);
    CHECK_EQ(RsxrTuner8_FreeSessionSize(session), rsxrtu::kSessionSize);
    RsxrTuner8_InitSession(session);  // TU8ALLOC remains installed after isolation.
    CHECK(RsxrTuner8_HasSession(session));
    CHECK(!RsxrTuner8_Apply(session, offs, 7, extra));
    CHECK_EQ(RsxrTuner8_FreeSessionSize(session), rsxrtu::kSessionSize);
    Rsxr_ResetFeatureFailures();
}

TEST(tuner8_order_is_lowest_string_first) {
    uint8_t o[8];

    rsxrtu::Order(6, o);

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(o[i], i);
    }

    rsxrtu::Order(4, o);

    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(o[i], i);
    }

    rsxrtu::Order(7, o);
    const uint8_t seven[] = {6, 0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(o[i], seven[i]);
    }

    rsxrtu::Order(8, o);
    const uint8_t eight[] = {7, 6, 0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(o[i], eight[i]);
    }

    // Steps: identity for six (stock cur+1 / cur-1 inside 0..n-1), the order for seven / eight.
    rsxrtu::Order(6, o);

    for (int c = 0; c < 6; ++c) {
        CHECK_EQ(rsxrtu::Step(o, 6, c, 1), c < 5 ? c + 1 : -1);
        CHECK_EQ(rsxrtu::Step(o, 6, c, -1), c > 0 ? c - 1 : -1);
    }

    CHECK_EQ(rsxrtu::Step(o, 6, 6, 1), -1);
    rsxrtu::Order(7, o);
    CHECK_EQ(rsxrtu::Step(o, 7, 6, 1), 0);
    CHECK_EQ(rsxrtu::Step(o, 7, 0, -1), 6);
    CHECK_EQ(rsxrtu::Step(o, 7, 6, -1), -1);
    CHECK_EQ(rsxrtu::Step(o, 7, 5, 1), -1);
    rsxrtu::Order(8, o);
    CHECK_EQ(rsxrtu::Step(o, 8, 7, 1), 6);
    CHECK_EQ(rsxrtu::Step(o, 8, 6, 1), 0);
    CHECK_EQ(rsxrtu::Step(o, 8, 6, -1), 7);
}

TEST(tuner8_targets_and_labels) {
    const int offs[6] = {-2, -2, -2, -2, -2, -2}, extra[2] = {-2, 1};
    int open[8], off[8];

    rsxrtu::Targets(offs, extra, open, off);
    const int want[8] = {38, 43, 48, 53, 57, 62, 33, 31};

    for (int s = 0; s < 8; ++s) {
        CHECK_EQ(open[s], want[s]);
        CHECK_EQ(off[s], s < 6 ? -2 : extra[s - 6]);
    }

    CHECK(!std::strcmp(rsxrtu::NoteName(35), "B"));
    CHECK(!std::strcmp(rsxrtu::NoteName(30), "F#"));
    CHECK(!std::strcmp(rsxrtu::NoteName(-1), "B"));
}

namespace {
struct Session {
    uint8_t b[rsxrtu::kSessionSize]{};

    Session() {
        g_rsxr_native_tuner = 1;

        for (int s = 0; s < 6; ++s) {
            reinterpret_cast<int32_t*>(b + rsxrtu::kStockOpen)[s] = 40 + 5 * s;
            reinterpret_cast<uint32_t*>(b + rsxrtu::kStockLetters)[2 * s] = 0x1000 + s;
            reinterpret_cast<uint32_t*>(b + rsxrtu::kStockLetters)[2 * s + 1] = 0x2000 + s;
            reinterpret_cast<uint32_t*>(b + rsxrtu::kStockPicks)[s] = 0x3000 + s;
            reinterpret_cast<int32_t*>(b + rsxrtu::kStockLast)[s] = s;
        }

        RsxrTuner8_InitSession(b);
    }

    ~Session() { RsxrTuner8_FreeSessionSize(b); }

    template <class T>
    T& at(uint32_t off) {
        return *reinterpret_cast<T*>(b + off);
    }
};

int g_restarts = 0, g_selected = -1;

void __fastcall EnterCheck(uintptr_t submode, uintptr_t, uint8_t* session) {
    CHECK_EQ(submode, uintptr_t(0));
    ++g_restarts;
    Put(session, rsxrtu::kMode, 0);  // the game enters check mode before selecting its first string
    RsxrMid::Context c{};

    c.ebx = uint32_t(uintptr_t(session));
    Rsxr_Mid_TS8F1(c);
    g_selected = rsxrtuner::UncheckedRead<int>(session, rsxrtu::kCur);
    RsxrTu8_OnSelect(session, uint32_t(g_selected));
}
}  // namespace

TEST(tuner8_apply_makes_a_native_session_and_moves_the_entities) {
    Switches save;
    Session s;
    const int offs[6] = {0, 0, 0, 0, 0, 0}, extra[2] = {0, 0};

    CHECK(!RsxrTuner8_Apply(s.b, offs, 6, extra));  // six strings: the game's code
    CHECK(!RsxrTuner8_IsNative(s.b));
    CHECK(RsxrTuner8_Apply(s.b, offs, 7, extra));
    CHECK(RsxrTuner8_IsNative(s.b));
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 7);
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kOrder), 6);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kLetters + 8 * k), 0x1000u + k);
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kStockLetters + 8 * k), 0u);  // moved, not copied
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kStockPicks + 4 * k), 0u);
        CHECK_EQ(s.at<int32_t>(rsxrtu::kLastPick + 4 * k), k);
    }

    CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen + 4 * 6), 35);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kLastPick + 4 * 6), -2);
    CHECK_EQ(s.at<uint32_t>(rsxrtu::kPicks + 4 * 6), 0u);
    // Re-applied: 8 strings, then 6 (a native session stays native, its count and order follow).
    const int extra8[2] = {-1, 2};

    CHECK(RsxrTuner8_Apply(s.b, offs, 8, extra8));
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 8);
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kOrder), 7);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen + 4 * 6), 34);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen + 4 * 7), 32);
    CHECK_EQ(s.at<uint32_t>(rsxrtu::kLetters), 0x1000u);  // entities stay in the tail
    CHECK(RsxrTuner8_Apply(s.b, offs, 6, extra));
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 6);
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kOrder), 0);
    CHECK(RsxrTuner8_IsNative(s.b));
    g_rsxr_native_tuner = 0;
    CHECK(RsxrTuner8_IsNative(s.b));
    CHECK(RsxrTuner8_Apply(s.b, offs, 7, extra));
    g_rsxr_native_tuner = 1;
}

TEST(tuner8_goes_native_for_a_seven_built_guitar_whatever_the_target) {
    // fix/menutuner (a): pure rule.
    CHECK(!rsxrtu::GoesNative(6, false, 6));
    CHECK(!rsxrtu::GoesNative(6, false, 0));  // no guitar bound yet
    CHECK(!rsxrtu::GoesNative(4, false, 4));  // bass
    CHECK(rsxrtu::GoesNative(6, false, 7));
    CHECK(rsxrtu::GoesNative(6, false, 8));
    CHECK(rsxrtu::GoesNative(7, false, 6));
    CHECK(rsxrtu::GoesNative(6, true, 6));
}

TEST(tuner8_extended_bass_starts_on_the_low_b) {
    // A 5/6-string bass goes native, its order starts on the low B (string 4) and ends on the G / high C.
    CHECK(rsxrtu::GoesNative(6, false, 5, true));
    CHECK(rsxrtu::GoesNative(6, false, 6, true));
    CHECK(!rsxrtu::GoesNative(4, false, 4, true));  // a four-string bass keeps the stock tuner

    uint8_t o[8];

    rsxrtu::Order(5, o, true);
    const uint8_t five[] = {4, 0, 1, 2, 3};

    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(o[i], five[i]);
    }

    CHECK_EQ(rsxrtu::Step(o, 5, 4, 1), 0);
    CHECK_EQ(rsxrtu::Step(o, 5, 3, 1), -1);
    rsxrtu::Order(6, o, true);
    const uint8_t six[] = {4, 0, 1, 2, 3, 5};

    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(o[i], six[i]);
    }

    CHECK_EQ(rsxrtu::Step(o, 6, 3, 1), 5);
    CHECK_EQ(rsxrtu::Step(o, 6, 0, -1), 4);
    rsxrtu::Order(4, o, true);  // four strings: the stock order

    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(o[i], i);
    }
}

TEST(tuner8_six_string_target_on_a_seven_built_guitar_is_a_native_session) {
    // The owner's crash (tests/owner-crash1): main-menu tuner, 6-string tuning, guitar built with 7 -> the stock
    // UI_PlayerTuner_SetupPicks 0x5B1E50 wrote +144h[6]. Now TS makes the session native (six strings, 8-slot tail).
    Switches save;
    Session s;
    Objects o;

    g_rsxr_native_strings = 1;
    std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);  // the session's guitar = o.guitar (native GuitarStrings)
    const int offs[6] = {0, 0, 0, 0, 0, 0}, extra[2] = {0, 0};

    o.info[0xB] = 6;
    CHECK_EQ(RsxrTuner8_GuitarCount(s.b), 6u);
    CHECK(!RsxrTuner8_Apply(s.b, offs, 6, extra));  // 6 on 6: the game's code, as before
    CHECK(!RsxrTuner8_IsNative(s.b));
    o.info[0xB] = 7;
    CHECK_EQ(RsxrTuner8_GuitarCount(s.b), 7u);
    CHECK(RsxrTuner8_Apply(s.b, offs, 6, extra));
    CHECK(RsxrTuner8_IsNative(s.b));
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 6);  // six pegs / strings tuned
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kOrder), 0);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kStockPicks + 4 * k), 0u);
    }

    CHECK_EQ(s.at<uint32_t>(rsxrtu::kPicks + 4 * 6), 0u);  // slot 6 exists in the tail (the crash slot)
}

TEST(tuner8_ensure_native_converts_only_stock_sessions_on_seven_built_guitars) {
    Switches save;
    Objects o;

    g_rsxr_native_strings = 1;

    {  // guitar built with 6 (and bass, and no guitar): stays stock
        Session s;

        std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);
        o.info[0xB] = 6;
        CHECK(!RsxrTuner8_EnsureNative(s.b, "test"));
        CHECK(!RsxrTuner8_IsNative(s.b));
        o.info[0xB] = 4;
        CHECK(!RsxrTuner8_EnsureNative(s.b, "test"));
        CHECK(!RsxrTuner8_IsNative(s.b));
        std::memset(s.b + 0x2C, 0, 4);
        CHECK(!RsxrTuner8_EnsureNative(s.b, "test"));
        CHECK_EQ(RsxrTuner8_GuitarCount(s.b), 0u);
    }

    {  // guitar built with 7: native, six strings, the session's own offsets (+50h)
        Session s;

        std::memcpy(s.b + 0x2C, o.session + 0x2C, 4);

        for (int k = 0; k < 6; ++k) {
            s.at<int32_t>(rsxrtu::kStockOff + 4 * k) = -2;
        }

        o.info[0xB] = 7;
        CHECK(RsxrTuner8_EnsureNative(s.b, "test"));
        CHECK(RsxrTuner8_IsNative(s.b));
        CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 6);

        for (int k = 0; k < 6; ++k) {
            CHECK_EQ(s.at<int32_t>(rsxrtu::kOffsets + 4 * k), -2);
        }

        CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen), 38);
        CHECK(RsxrTuner8_EnsureNative(s.b, "test"));  // idempotent
    }

    {  // the guitar has no native GuitarStrings: cannot be made native (logged), stays stock
        Session s;
        Objects bad;

        std::memcpy(s.b + 0x2C, bad.session + 0x2C, 4);
        bad.info[0xB] = 7;
        Put(bad.strings, rsxrgs::kMagicOff, uint32_t(0));
        CHECK(!RsxrTuner8_EnsureNative(s.b, "test"));
        CHECK(!RsxrTuner8_IsNative(s.b));
    }

    {  // an unregistered (stock-sized) session: never touched
        uint8_t stock[rsxrtu::kSessionSize]{};

        std::memcpy(stock + 0x2C, o.session + 0x2C, 4);
        o.info[0xB] = 7;
        CHECK(!RsxrTuner8_EnsureNative(stock, "test"));
        CHECK_EQ(stock[rsxrtu::kNative], 0);
    }
}

TEST(tuner8_apply_needs_a_registered_grown_session) {
    Switches save;

    g_rsxr_native_tuner = 1;
    uint8_t stock[rsxrtu::kSessionSize]{};
    const int offs[6]{}, extra[2]{};

    CHECK(!RsxrTuner8_Apply(stock, offs, 7, extra));
    CHECK_EQ(stock[rsxrtu::kNative], 0);
}

TEST(tuner8_deferred_conversion_keeps_each_sessions_full_target) {
    Switches save;
    rsxrtest::Restore restore_keys(rsxrtuner::g_state.keys);
    rsxrtest::Restore restore_entry(rsxrtuner::g_state.enterCheck);

    rsxrtuner::g_state.enterCheck = uintptr_t(&EnterCheck);

    for (int count : {7, 8}) {
        Objects guitar, bass;
        Session a, b;
        int offs[6] = {-1, -2, -3, -4, -5, -6};
        int bassOffs[6] = {1, 2, 3, 4, -2, -1};
        rsxrtuner::Key key{true, -3, -3.0};

        key.found7 = count == 8;
        key.t7 = -4;
        rsxrtuner::g_state.keys.Put(offs, key);
        rsxrtuner::Key bassKey{false, 0, 0.0};

        bassKey.bass = count - 2;
        rsxrtuner::g_state.keys.Put(bassOffs, bassKey);
        guitar.info[0xB] = uint8_t(count);
        bass.info[0] = 2;
        bass.info[0xB] = uint8_t(count - 2);
        Put(a.b, 0x2C, guitar.guitar);
        Put(b.b, 0x2C, bass.guitar);
        g_rsxr_native_strings = 1;
        g_rsxr_fg_row = rsxrfg::kRowStock;
        Rsxr_TunerApply(a.b, offs);
        Rsxr_TunerApply(b.b, bassOffs);
        CHECK(!RsxrTuner8_IsNative(a.b));
        CHECK(!RsxrTuner8_IsNative(b.b));
        std::memset(offs, 0, sizeof offs);
        std::memset(bassOffs, 0, sizeof bassOffs);
        CHECK(!RsxrTuner8_EnsureNative(a.b, "test pending"));
        g_rsxr_fg_row = rsxrfg::kRowNative;
        CHECK(RsxrTuner8_EnsureNative(a.b, "test ready"));
        CHECK(RsxrTuner8_EnsureNative(b.b, "test ready"));
        CHECK_EQ(a.at<uint8_t>(rsxrtu::kCount), count);
        CHECK_EQ(b.at<uint8_t>(rsxrtu::kCount), count - 2);
        CHECK_EQ(a.at<int>(rsxrtu::kOffsets), -1);
        CHECK_EQ(a.at<int>(rsxrtu::kOffsets + 24), -3);
        CHECK_EQ(a.at<int>(rsxrtu::kOffsets + 28), count == 8 ? -4 : 0);
        CHECK_EQ(b.at<int>(rsxrtu::kOffsets), 1);
        CHECK_EQ(b.at<int>(rsxrtu::kOffsets + 16), -2);
        CHECK_EQ(b.at<int>(rsxrtu::kOpen + 16), 21);
        CHECK_EQ(b.at<uint8_t>(rsxrtu::kOrder), 4);
    }
}

TEST(tuner8_deferred_conversion_restarts_selection_and_visits_every_target_string) {
    Switches save;
    rsxrtest::Restore restore_entry(rsxrtuner::g_state.enterCheck);
    rsxrtest::Restore restore_copy(rsxrtuner::g_state.sessCopy[0]);

    rsxrtuner::g_state.enterCheck = uintptr_t(&EnterCheck);
    rsxrtuner::g_state.sessCopy[0] = 0x1234;

    for (int count : {5, 6, 7, 8}) {
        for (int mode : {0, 1, 2, 3}) {
            Session s;
            const int offs[6] = {}, extra[2] = {};
            const bool bass = count <= 6;

            g_rsxr_fg_row = rsxrfg::kRowStock;
            CHECK(!RsxrTuner8_Apply(s.b, offs, count, extra, bass ? count : 0));
            s.at<int>(rsxrtu::kMode) = mode;  // the stock tuner starts before the deferred layout is ready
            s.at<int>(rsxrtu::kCur) = 0;
            g_restarts = 0;
            g_selected = -1;
            CHECK(!RsxrTuner8_EnsureNative(s.b, "test pending"));
            CHECK_EQ(g_restarts, 0);
            g_rsxr_fg_row = rsxrfg::kRowNative;
            RsxrMid::Context c{};
            uint32_t stack[2] = {};

            c.esp = uint32_t(uintptr_t(stack));
            c.esi = uint32_t(uintptr_t(s.b));
            Rsxr_Mid_TS8SEL(c);
            CHECK(RsxrTuner8_IsNative(s.b));
            CHECK_EQ(c.edi, uint32_t(bass ? 4 : count - 1));
            CHECK_EQ(g_restarts, 1);
            CHECK_EQ(s.at<int>(rsxrtu::kMode), 0);
            CHECK_EQ(g_selected, bass ? 4 : count - 1);
            const int guitar7[] = {6, 0, 1, 2, 3, 4, 5};
            const int guitar8[] = {7, 6, 0, 1, 2, 3, 4, 5};
            const int bassOrder[] = {4, 0, 1, 2, 3, 5};
            const int* want = bass ? bassOrder : count == 7 ? guitar7 : guitar8;

            for (int i = 0; i < count; ++i) {
                CHECK_EQ(s.at<int>(rsxrtu::kCur), want[i]);
                CHECK_EQ(RsxrTu8_AdvanceC(s.b, 1), i + 1 < count ? 1 : 0);
            }

            CHECK(RsxrTuner8_EnsureNative(s.b, "test already native"));
            CHECK_EQ(g_restarts, 1);
        }
    }
}

TEST(tuner8_known_target_count_is_independent_of_the_built_guitar_count) {
    Switches save;
    rsxrtest::Restore restore_keys(rsxrtuner::g_state.keys);
    Objects o;
    const int offs[6] = {};

    g_rsxr_native_strings = 1;
    o.info[0xB] = 8;

    for (int target : {6, 7, 8, 0}) {
        Session s;

        Put(s.b, 0x2C, o.guitar);
        rsxrtuner::g_state.keys.Clear();

        if (target) {
            rsxrtuner::Key key{target > 6, -2, -2.0};

            key.found7 = target == 8;
            rsxrtuner::g_state.keys.Put(offs, key);
        }

        Rsxr_TunerApply(s.b, offs);
        CHECK(RsxrTuner8_IsNative(s.b));
        CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), target ? target : 8);
        CHECK_EQ(s.at<int>(rsxrtu::kOffsets + 24), target > 6 ? -2 : 0);
    }
}

TEST(tuner8_freed_session_does_not_reuse_a_deferred_target) {
    Switches save;
    Session s;
    Objects o;
    const int offs[6] = {}, extra[2] = {-2, -3};

    g_rsxr_native_strings = 1;
    o.info[0xB] = 8;
    Put(s.b, 0x2C, o.guitar);
    g_rsxr_fg_row = rsxrfg::kRowStock;
    CHECK(!RsxrTuner8_Apply(s.b, offs, 8, extra));
    CHECK_EQ(RsxrTuner8_FreeSessionSize(s.b), rsxrtu::kSessionSize);
    RsxrTuner8_InitSession(s.b);
    g_rsxr_fg_row = rsxrfg::kRowNative;
    CHECK(RsxrTuner8_EnsureNative(s.b, "test reused"));
    CHECK_EQ(s.at<uint8_t>(rsxrtu::kCount), 6);
    CHECK_EQ(s.at<int>(rsxrtu::kOffsets + 24), 0);
}

TEST(tuner8_mid_hooks_follow_the_order_and_leave_stock_sessions_alone) {
    Switches save;
    Session s;
    RsxrMid::Context c{};

    c.ebx = uint32_t(uintptr_t(s.b));
    c.esi = uint32_t(uintptr_t(s.b));
    c.ecx = uint32_t(uintptr_t(s.b));
    c.jump = 0x1234;
    Rsxr_Mid_TS8F1(c);
    Rsxr_Mid_TS8F2(c);
    Rsxr_Mid_TS8F3(c);  // stock session: replay only
    CHECK_EQ(c.jump, 0x1234u);
    CHECK_EQ(c.edi, 0u);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kCur), 0);
    const int offs[6]{}, extra[2]{};

    CHECK(RsxrTuner8_Apply(s.b, offs, 7, extra));
    Rsxr_Mid_TS8F1(c);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kCur), 6);
    CHECK_EQ(c.jump, 0x5AFB1Eu);
    CHECK_EQ(c.esi, c.ebx);
    s.at<int32_t>(rsxrtu::kCur) = 3;
    c.jump = 0x1234;
    Rsxr_Mid_TS8F2(c);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kCur), 6);
    CHECK_EQ(c.jump, 0x1234u);
    Rsxr_Mid_TS8F3(c);
    CHECK_EQ(c.edi, 6u);
    c.edi = 5;
    s.at<uint8_t>(0x22D) = 1;  // reverse pass: the stock 5 stays
    Rsxr_Mid_TS8F3(c);
    CHECK_EQ(c.edi, 5u);
    // Reset: the tail takes the standard tuning, zero peaks.
    s.at<int32_t>(rsxrtu::kOpen + 4 * 6) = 99;
    s.at<float>(rsxrtu::kPeaks) = 1.0f;
    Rsxr_Mid_TS8RS(c);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen + 4 * 6), 35);
    CHECK_EQ(s.at<int32_t>(rsxrtu::kOpen), 40);
    CHECK(s.at<float>(rsxrtu::kPeaks) == 0.0f);
    // Wrong instrument: the target of the string at [ebp+8].
    uint32_t frame[4]{0, 0, 6, 0};

    c.ebp = uint32_t(uintptr_t(frame));
    c.jump = 0;
    Rsxr_Mid_TS8WR(c);
    CHECK_EQ(c.ecx, 6u);
    CHECK_EQ(c.edx, 35u);
    CHECK_EQ(c.jump, 0x5B2D8Fu);
    // Tuned sound: the note of string 6 itself (EDI is clamped to 5 by then).
    uint32_t stack[2]{0, uint32_t(uintptr_t(s.b))};

    c.esp = uint32_t(uintptr_t(stack));
    c.ecx = 6;
    Rsxr_Mid_TS8SN1(c);
    uint32_t local[16]{};

    local[16 - 9] = uint32_t(uintptr_t(s.b));  // [ebp-24h]
    c.ebp = uint32_t(uintptr_t(local + 16));
    c.jump = 0;
    c.eax = 0;
    Rsxr_Mid_TS8SN2(c);
    CHECK_EQ(c.eax, 35u);
    CHECK_EQ(c.jump, 0x5B2EAEu);
}

TEST(tuner8_dtor_hands_back_strings_0_to_5) {
    Switches save;
    Session s;
    const int offs[6]{}, extra[2]{};

    CHECK(RsxrTuner8_Apply(s.b, offs, 7, extra));
    RsxrMid::Context c{};

    c.ecx = uint32_t(uintptr_t(s.b));
    Rsxr_Mid_TS8DT(c);

    for (int k = 0; k < 6; ++k) {
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kStockLetters + 8 * k), 0x1000u + k);
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kStockPicks + 4 * k), 0x3000u + k);
        CHECK_EQ(s.at<uint32_t>(rsxrtu::kPicks + 4 * k), 0u);
    }

    CHECK(!RsxrTuner8_IsNative(s.b));
}

TEST(tuner8_shared_tuner_sites_stay_and_the_b_phase_is_gone) {
    Switches save;

    g_rsxr_native_fretgrid = 1;
    g_rsxr_native_colours = 1;

    // 1.0: the six-slot B-phase tuner sites were removed with their stubs.
    for (const char* id :
         {"T1", "TA", "TR", "M1", "M2", "L1", "L2", "TLC", "TLI", "TLO1", "TLO2", "TF", "PK", "PE", "TD"}) {
        CHECK(Rsxr_FindHook(id) == nullptr);
    }

    for (const char* id : {"TK", "TS", "W", "AT"}) {
        const auto* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        g_rsxr_native_tuner = 1;
        CHECK(Rsxr_ShouldPatch(*h, true, false, true, true));
    }
}

TEST(tuner8_sessions_stay_stock_until_the_fretgrid_layout_is_written) {
    Switches save;
    Session s;
    const int offs[6]{}, extra[2]{};

    g_rsxr_fg_row = rsxrfg::kRowStock;  // TS8PKS / TS8PKP carry the native row stride
    CHECK(!RsxrTuner8_Apply(s.b, offs, 7, extra));
    CHECK(!RsxrTuner8_IsNative(s.b));
    g_rsxr_fg_row = rsxrfg::kRowNative;
    CHECK(RsxrTuner8_Apply(s.b, offs, 7, extra));
}
