#include "features/tones/game.h"
#include "features/tones/state.h"
#include "features/tones/tones.h"
#include "json_fixture.h"
#include "platform/log.h"
#include "test.h"
#include "testutil.h"
#include <array>
#include <vector>

extern "C" uintptr_t g_rsxr_tone_apply, g_rsxr_tone_manager;
extern "C" uintptr_t g_rsxr_tone_context;
extern "C" void Rsxr_Mid_ToneAutoBefore(RsxrMid::Context&);
extern "C" void Rsxr_Mid_ToneAutoAfter(RsxrMid::Context&);
extern "C" void Rsxr_Mid_ToneLoad(RsxrMid::Context&);

namespace {
uint32_t g_objects[8]{}, g_players[8]{}, g_calls = 0;

void ApplyObject(RsxrMid::Context& c) {
    if (g_calls < 8) {
        g_objects[g_calls] = RsxrMid::Dword(c.esi);
        g_players[g_calls] = RsxrMid::Dword(c.esp + 8);
    }

    ++g_calls;
    c.eax = 1;
}

RsxrMid::CallIn g_applyObject{&ApplyObject, RsxrMid::Mode::Full, 12};

std::vector<std::string> g_logs;

void CaptureLog(const char* text) {
    g_logs.emplace_back(text);
}

struct Logging {
    Logging() {
        g_logs.clear();
        RsxrLog_SetSink(CaptureLog);
    }

    ~Logging() { RsxrLog_SetSink(nullptr); }
};

size_t Lines(const char* text) {
    size_t count = 0;

    for (const auto& line : g_logs) {
        count += line.find(text) != std::string::npos;
    }

    return count;
}
}  // namespace

TEST(tone_names_use_the_manifest_string_accessor) {
    // 0x8899F0 reads virtual +60h. +64h is intentionally absent from this fixture.
    tonetest::Node attributes, key("Cleanalt");
    char text[256]{};

    attributes.members["Tone_E"] = &key;
    CHECK(rsxrtones::Name(tonetest::Addr(&attributes), "Tone_E", text, sizeof text));
    CHECK(std::strcmp(text, "Cleanalt") == 0);
    CHECK(!rsxrtones::Name(tonetest::Addr(&attributes), "Tone_F", text, sizeof text));
}

TEST(tone_load_hook_resolves_attributes_from_the_parent_arrangement) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);
    const Logging logging;
    tonetest::Node parent, attributes, marker, echo("echo"), foxtrot("foxtrot");
    const rsxrtones::Identity id{0x6789, 2, 3, 4};
    std::array<uint32_t, 256> frame{}, session{};
    RsxrMid::Context context{};
    const uint32_t song = 0x6789AB, address = tonetest::Addr(session.data());

    g_rsxr_tones = 1;
    parent.members["Attributes"] = &attributes;
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &echo;
    attributes.members["Tone_F"] = &foxtrot;
    context.ebp = tonetest::Addr(frame.data()) + 0x3A0;
    frame[0x3AC / 4] = tonetest::Addr(&parent);  // [EBP+0Ch] is the parent, as at 0x56774C
    std::memcpy(frame.data(), id.data(), sizeof id);
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    session[0x78 / 4] = song;
    Rsxr_Mid_ToneLoad(context);
    CHECK_EQ(Lines("extra tones=2 slots=E,F marker=26 captured"), size_t(1));
    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK(rsxrtones::Handle(address, true, 5, false));
    parent.members.clear();
    Rsxr_Mid_ToneLoad(context);  // missing Attributes must discard the previous capture
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    RsxrTones_SongLoaded(song, false);
}

TEST(tone_load_decisions_and_first_event_are_bounded_even_when_application_fails) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);
    const Logging logging;
    tonetest::Node attributes, marker, extra("missing");
    const rsxrtones::Identity id{0x4567, 2, 3, 4};
    std::array<uint32_t, 256> session{};
    const uint32_t song = 0x456789, address = tonetest::Addr(session.data());
    const float time = 8.25f;

    g_rsxr_tones = 1;
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &extra;
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x3B4, &time, sizeof time);
    session[0x78 / 4] = song;
    rsxrtones::Capture(tonetest::Addr(&attributes), 0, id);
    RsxrTones_SongLoaded(song, true);

    for (int i = 0; i < 4; ++i) {
        CHECK(rsxrtones::Handle(address, true, 4, false));
    }

    CHECK_EQ(Lines("player=0 extra tones 1 (E), active"), size_t(1));
    CHECK_EQ(Lines("first event raw=4 time=8.250"), size_t(1));
    CHECK_EQ(Lines("id=4 key='missing' rejected"), size_t(1));
    CHECK_EQ(Lines("register player=0 slot=5 key='missing' rejected: missing or ambiguous definition"), size_t(1));
    CHECK(rsxrtones::Handle(address, false, 0, false));
    CHECK(rsxrtones::Handle(address, true, 0, false));
    CHECK_EQ(Lines("id=26 key='' rejected"), size_t(1));
    CHECK_EQ(Lines("id=0 key='' rejected"), size_t(1));
    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(address, true, 5, false));
    CHECK_EQ(Lines("first event raw="), size_t(2));
    CHECK_EQ(Lines("player=0 extra tones 1 (E), active"), size_t(2));
    CHECK_EQ(Lines("register player=0 slot=5 key='missing' rejected: missing or ambiguous definition"), size_t(2));
    RsxrTones_SongLoaded(song, false);
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(Lines("rejected: SNG is not native"), size_t(1));
    rsxrtones::Capture(0, 0, id);
}

TEST(tone_inactive_sessions_explain_missing_capture_marker_and_extra_tones) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);
    const Logging logging;
    tonetest::Node attributes, extra("Cleanalt");
    const rsxrtones::Identity id{0x5678, 2, 3, 4};
    std::array<uint32_t, 256> session{};
    const uint32_t song = 0x56789A, address = tonetest::Addr(session.data());

    g_rsxr_tones = 1;
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    session[0x78 / 4] = song;
    RsxrTones_SongLoaded(song, true);
    CHECK(!rsxrtones::Handle(address, false, 0, false));
    CHECK(!rsxrtones::Handle(address, false, 0, false));
    CHECK_EQ(Lines("rejected: manifest not captured"), size_t(1));
    rsxrtones::Capture(tonetest::Addr(&attributes), 0, id);
    CHECK(!rsxrtones::Handle(address, false, 0, false));
    CHECK_EQ(Lines("player=0 no extra tones"), size_t(1));
    attributes.members["Tone_E"] = &extra;
    rsxrtones::Capture(tonetest::Addr(&attributes), 0, id);
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(Lines("player=0 rejected: missing or invalid RsxrToneSlots"), size_t(1));  // the capture logs it too
    g_rsxr_tones = 0;
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(Lines("rejected: feature disabled"), size_t(1));
    g_rsxr_tones = 1;
    rsxrtones::Capture(0, 0, id);
    RsxrTones_SongLoaded(song, false);
}

TEST(tone_definition_lookup_rejects_ambiguous_case_insensitive_keys) {
    tonetest::Node first, second, firstKey("crunch"), secondKey("CRUNCH");

    first.members["Key"] = &firstKey;
    second.members["Key"] = &secondKey;

    uint32_t nodes[] = {tonetest::Addr(&first), tonetest::Addr(&second)};
    uint32_t collection[6]{};

    collection[4] = tonetest::Addr(nodes);
    collection[5] = collection[4] + 4;
    CHECK_EQ(rsxrtones::Definition(tonetest::Addr(collection), "CRUNCH"), tonetest::Addr(&first));
    CHECK_EQ(first.refs, uint32_t(1));
    CHECK(rsxrtones::Reference(tonetest::Addr(&first), false));
    collection[5] += 4;
    CHECK_EQ(rsxrtones::Definition(tonetest::Addr(collection), "crunch"), uint32_t(0));
    first.canonical = false;
    CHECK_EQ(rsxrtones::Definition(tonetest::Addr(collection), "crunch"), uint32_t(0));
    CHECK_EQ(first.refs, uint32_t(0));
    CHECK_EQ(second.refs, uint32_t(0));
}

TEST(tone_state_requires_native_sng_and_discards_replaced_manifest_data) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);
    tonetest::Node attributes, marker, extra("missing-definition");
    const rsxrtones::Identity id{0x1234, 2, 3, 4};
    std::array<uint32_t, 256> session{};
    const uint32_t song = 0x123456, address = tonetest::Addr(session.data());

    g_rsxr_tones = 1;
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &extra;
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    session[0x78 / 4] = song;
    rsxrtones::Capture(tonetest::Addr(&attributes), 0, id);
    RsxrTones_SongLoaded(song, false);
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(address, true, 4, false));  // missing definition is owned/rejected, never a stock ID
    CHECK(rsxrtones::Handle(address, true, 5, false));  // F must not become the stock sentinel
    CHECK(rsxrtones::Handle(address, true, 1000, false));
    RsxrTones_SongLoaded(song, false);
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    RsxrTones_SongLoaded(song, true);
    rsxrtones::Capture(0, 0, id);  // same arrangement identity, now without native extra fields
    CHECK(!rsxrtones::Handle(address, true, 4, false));
    RsxrTones_SongLoaded(song, false);
}

TEST(tone_players_apply_their_own_definitions_and_reload_resets_selection) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones), restoreApply(g_rsxr_tone_apply),
        restoreManager(g_rsxr_tone_manager);
    tonetest::Node attributes[2], definitions[2], marker, keys[2]{tonetest::Node("first"), tonetest::Node("second")};
    std::array<uint32_t, 256> sessions[2]{};
    const rsxrtones::Identity ids[2]{{0x2345, 2, 3, 4}, {0x3456, 2, 3, 4}};
    uint32_t manager = 0x1234, nodes[2]{}, collections[2][6]{};
    const uint32_t song = 0x234567;

    g_rsxr_tones = 1;
    g_rsxr_tone_apply = g_applyObject.Entry();
    g_rsxr_tone_manager = reinterpret_cast<uintptr_t>(&manager);
    g_calls = 0;
    CHECK(g_rsxr_tone_apply);

    for (uint32_t i = 0; i < 2; ++i) {
        attributes[i].members["RsxrToneSlots"] = &marker;
        attributes[i].members["Tone_E"] = &keys[i];
        definitions[i].members["Key"] = &keys[i];
        nodes[i] = tonetest::Addr(&definitions[i]);
        collections[i][4] = tonetest::Addr(&nodes[i]);
        collections[i][5] = collections[i][4] + 4;
        sessions[i][2] = i;
        sessions[i][0x78 / 4] = song;
        sessions[i][0x228 / 4] = nodes[i];  // Base for the before-first-event check
        std::memcpy(reinterpret_cast<uint8_t*>(sessions[i].data()) + 0x68, ids[i].data(), sizeof ids[i]);
        rsxrtones::Capture(tonetest::Addr(&attributes[i]), tonetest::Addr(collections[i]), ids[i]);
        CHECK_EQ(definitions[i].refs, uint32_t(1));
    }

    attributes[1].members["Tone_F"] = &keys[1];
    rsxrtones::Capture(tonetest::Addr(&attributes[1]), tonetest::Addr(collections[1]), ids[1]);

    const uint32_t first = tonetest::Addr(sessions[0].data()), second = tonetest::Addr(sessions[1].data());

    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(first, true, 4, false));
    CHECK(rsxrtones::Handle(second, true, 4, false));
    CHECK(rsxrtones::Handle(first, true, 4, false));
    CHECK_EQ(g_calls, uint32_t(2));
    CHECK_EQ(g_objects[0], nodes[0]);
    CHECK_EQ(g_players[0], uint32_t(0));
    CHECK_EQ(g_objects[1], nodes[1]);
    CHECK_EQ(g_players[1], uint32_t(1));

    uint32_t raw = 5, stack[7]{};
    RsxrMid::Context context{};

    context.esp = tonetest::Addr(stack);
    context.esi = second;
    context.eax = 1;
    stack[6] = tonetest::Addr(&raw);
    Rsxr_Mid_ToneAutoBefore(context);
    Rsxr_Mid_ToneAutoAfter(context);
    CHECK_EQ(g_calls, uint32_t(3));
    CHECK_EQ(g_objects[2], nodes[1]);
    CHECK_EQ(g_players[2], uint32_t(1));
    CHECK_EQ(raw, rsxrtones::kInvalid);  // stock dispatch must never see F as its no-tone sentinel
    CHECK((context.eax & 0xFF) == 1);
    RsxrTones_SongLoaded(song, true);  // native reload at the same song and session addresses
    CHECK(rsxrtones::Handle(first, true, 4, false));
    CHECK_EQ(g_calls, uint32_t(4));
    CHECK(rsxrtones::Handle(first, false, 0, true));
    CHECK_EQ(g_calls, uint32_t(5));
    CHECK(rsxrtones::Handle(first, true, 5, false));  // undeclared F is rejected, not Base
    CHECK_EQ(g_calls, uint32_t(5));

    for (uint32_t i = 0; i < 2; ++i) {
        rsxrtones::Capture(0, 0, ids[i]);
        CHECK(!rsxrtones::Handle(tonetest::Addr(sessions[i].data()), true, 4, false));
        CHECK_EQ(definitions[i].refs, uint32_t(0));
    }

    RsxrTones_SongLoaded(song, false);
}

TEST(toneplan_playback_honours_replacement_and_reset_without_a_new_timeline_id) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones), restoreApply(g_rsxr_tone_apply),
        restoreManager(g_rsxr_tone_manager), restoreContext(g_rsxr_tone_context);
    const Logging logging;
    tonetest::Context context;
    tonetest::Node attributes, marker, key("echo"), original, edited, replacement;
    const rsxrtones::Identity id{0xABCDE, 2, 3, 4};
    std::array<uint32_t, 256> session{};
    uint32_t manager = 0x1234, node = tonetest::Addr(&original), collection[6]{};
    const uint32_t song = 0xABCDEF, address = tonetest::Addr(session.data());

    g_rsxr_tones = 1;
    g_rsxr_tone_apply = g_applyObject.Entry();
    g_rsxr_tone_manager = tonetest::Addr(&manager);
    g_rsxr_tone_context = context.Global();
    g_calls = 0;
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &key;
    original.members["Key"] = &key;
    edited.members["Key"] = &key;
    replacement.members["Key"] = &key;  // same Key, different object: still must reapply
    collection[4] = tonetest::Addr(&node);
    collection[5] = collection[4] + 4;
    session[0x78 / 4] = song;
    session[0x228 / 4] = node;
    session[0x22C / 4] = node;
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    rsxrtones::Capture(tonetest::Addr(&attributes), tonetest::Addr(collection), id);
    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(g_objects[0], node);
    context.players[0].members["LAS_ToneCustomData_ToneE"] = &edited;
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(g_calls, 2u);
    CHECK_EQ(g_objects[1], tonetest::Addr(&edited));
    CHECK_EQ(edited.refs, 1u);
    CHECK_EQ(Lines("override slot=E key='echo' applied"), size_t(1));
    context.players[0].members["LAS_ToneCustomData_ToneE"] = &replacement;
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(g_objects[2], tonetest::Addr(&replacement));
    CHECK_EQ(edited.refs, 0u);
    context.players[0].members.erase("LAS_ToneCustomData_ToneE");
    CHECK(rsxrtones::Handle(address, true, 4, false));
    CHECK_EQ(g_objects[3], node);
    CHECK_EQ(replacement.refs, 0u);
    CHECK_EQ(Lines("override slot=E key='echo' none"), size_t(2));
    context.players[0].members["LAS_ToneCustomData_ToneBase"] = &edited;
    context.players[0].members["LAS_ToneCustomData_ToneA"] = &replacement;
    CHECK(rsxrtones::Handle(address, false, 0, false));
    CHECK_EQ(g_objects[4], tonetest::Addr(&edited));
    CHECK(rsxrtones::Handle(address, true, 0, false));
    CHECK_EQ(g_objects[5], tonetest::Addr(&replacement));
    RsxrTones_SongLoaded(song, false);
    CHECK(!rsxrtones::Handle(address, true, 0, false));
    CHECK_EQ(replacement.refs, 0u);
    CHECK_EQ(g_calls, 6u);
    context.players[0].members["LAS_ToneCustomData_ToneZ"] = &edited;
    RsxrTones_SongLoaded(song, true);
    CHECK(rsxrtones::Handle(address, true, 25, false));  // unused Z cannot borrow a stale context override
    CHECK_EQ(g_calls, 6u);
    CHECK_EQ(edited.refs, 0u);
    rsxrtones::Capture(0, 0, id);
    CHECK(!rsxrtones::Handle(address, true, 0, false));
    CHECK_EQ(original.refs, 0u);
}
