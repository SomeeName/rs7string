#include "features/tones/game.h"
#include "features/tones/native_registration.h"
#include "features/tones/state.h"
#include "features/tones/tones.h"
#include "json_fixture.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"

extern "C" uintptr_t g_rsxr_tone_register, g_rsxr_tone_destroy;
extern "C" uintptr_t g_rsxr_tone_manager;
extern "C" void Rsxr_Mid_ToneRegisterAfter(RsxrMid::Context&);

namespace {
struct Calls {
    uint32_t player = 0, slot = 0, definition = 0, entry = 0, manager = 0, count = 0;
    uint32_t destroyed = 0, records = 0, firstEntry = 0, firstOffset = 0, released = 0;
};

Calls g_calls;
uint32_t g_resourceRefs = 0;
uint32_t g_resources[]{tonetest::Addr(&g_resourceRefs)};

void RegisterObject(RsxrMid::Context& c) {
    g_calls.player = c.eax;
    g_calls.manager = RsxrMid::Dword(c.esp + 4);
    g_calls.definition = RsxrMid::Dword(c.esp + 8);
    g_calls.slot = RsxrMid::Dword(c.esp + 12);
    const uint32_t store = rsxrtones::Read(g_calls.manager, 0x1A4);

    g_calls.entry = rsxrtones::Read(store, 8) + 12 * (8 + 5 * c.eax + g_calls.slot);
    if (!g_calls.count) {
        g_calls.firstEntry = g_calls.entry;
    }

    auto* record = RsxrMid::Ptr<uint32_t>(g_calls.entry);

    record[0] = tonetest::Addr(g_resources);
    record[1] = record[0] + 4;
    record[2] = record[1];
    ++g_resourceRefs;
    ++g_calls.count;
    (void)rsxrtones::Reference(g_calls.definition, false);  // stock consumes its by-value argument
    c.eax = 0xDEADBEEF;                                     // this is a void routine, not an acceptance flag
}

RsxrMid::CallIn g_register{&RegisterObject, RsxrMid::Mode::Full, 12};

void __cdecl DestroyRecords(uint32_t end, uint32_t begin) {
    ++g_calls.destroyed;
    g_calls.records = (end - begin) / 12;
    g_calls.firstOffset = (g_calls.firstEntry - begin) / 12;

    for (uint32_t at = begin; at < end; at += 12) {
        const uint32_t resourceBegin = rsxrtones::Read(at), resourceEnd = rsxrtones::Read(at, 4);

        if (!resourceBegin) {
            CHECK_EQ(resourceEnd, uint32_t(0));
            continue;
        }

        CHECK_EQ(resourceBegin, tonetest::Addr(g_resources));
        CHECK_EQ(resourceEnd, resourceBegin + 4);
        CHECK_EQ(rsxrtones::Read(at, 8), resourceEnd);
        CHECK_EQ(rsxrtones::Read(resourceBegin), tonetest::Addr(&g_resourceRefs));
        CHECK(g_resourceRefs > 0);
        --g_resourceRefs;
        ++g_calls.released;
    }
}
}  // namespace

TEST(tone_native_registration_uses_an_isolated_store_and_preserves_definition_ownership) {
    rsxrtest::Restore<uintptr_t> restoreRegister(g_rsxr_tone_register), restoreDestroy(g_rsxr_tone_destroy);
    tonetest::Node definition;

    g_calls = {};
    g_rsxr_tone_register = g_register.Entry();
    g_rsxr_tone_destroy = reinterpret_cast<uintptr_t>(&DestroyRecords);
    CHECK(g_rsxr_tone_register);

    {
        rsxrtones::NativeRegistration store;

        CHECK(store.Register(tonetest::Addr(&definition), 0, 5));
        CHECK_EQ(definition.refs, uint32_t(0));
        CHECK_EQ(g_calls.definition, tonetest::Addr(&definition));
        CHECK_EQ(g_calls.player, uint32_t(0));
        CHECK_EQ(g_calls.slot, uint32_t(0));
        const uint32_t first = g_calls.entry;

        CHECK(store.Register(tonetest::Addr(&definition), 1, 5));
        CHECK_EQ(g_calls.player, uint32_t(1));
        CHECK_EQ(g_calls.entry - first, uint32_t(27 * 12));
        CHECK(store.Register(tonetest::Addr(&definition), 0, 26));
        CHECK_EQ(g_calls.entry - first, uint32_t(21 * 12));
        CHECK(store.Register(tonetest::Addr(&definition), 1, 26));
        CHECK_EQ(g_calls.entry - first, uint32_t(48 * 12));
        CHECK_EQ(g_calls.destroyed, uint32_t(0));
        CHECK(!store.Register(0, 0, 5));
        CHECK(!store.Register(tonetest::Addr(&definition), 2, 5));
        CHECK(!store.Register(tonetest::Addr(&definition), 0, 4));
        CHECK(!store.Register(tonetest::Addr(&definition), 0, 27));
        CHECK_EQ(g_calls.count, uint32_t(4));
        CHECK_EQ(g_resourceRefs, uint32_t(4));
    }

    CHECK_EQ(g_calls.destroyed, uint32_t(1));
    CHECK_EQ(g_calls.records, uint32_t(62));
    CHECK_EQ(g_calls.firstOffset, uint32_t(13));  // absolute Base/A..D reservation, not just relative spacing
    CHECK_EQ(g_calls.released, uint32_t(4));
    CHECK_EQ(g_resourceRefs, uint32_t(0));
    CHECK_EQ(definition.refs, uint32_t(0));
}

TEST(tone_native_registration_rejects_unavailable_cleanup_before_taking_a_reference) {
    rsxrtest::Restore<uintptr_t> restoreRegister(g_rsxr_tone_register), restoreDestroy(g_rsxr_tone_destroy);
    tonetest::Node definition;
    rsxrtones::NativeRegistration store;

    g_calls = {};
    g_rsxr_tone_register = g_register.Entry();
    g_rsxr_tone_destroy = 0;
    CHECK(!store.Register(tonetest::Addr(&definition), 0, 5));
    CHECK_EQ(g_calls.count, uint32_t(0));
    CHECK_EQ(definition.refs, uint32_t(0));
}

TEST(tone_native_registration_hook_requires_native_data_and_attempts_once_per_load) {
    rsxrtest::Restore<uintptr_t> restoreRegister(g_rsxr_tone_register), restoreDestroy(g_rsxr_tone_destroy),
        restoreManager(g_rsxr_tone_manager), restoreGate(g_rsxr_tones);
    tonetest::Node attributes, definition, marker, key("registered-echo");
    const rsxrtones::Identity id{0x8910, 2, 3, 4};
    std::array<uint32_t, 256> session{};
    uint32_t node = tonetest::Addr(&definition), collection[6]{}, manager = 0x1234, stack[6]{};
    const uint32_t address = tonetest::Addr(session.data()), song = 0x891011;
    RsxrMid::Context context{};

    g_calls = {};
    g_rsxr_tones = 1;
    g_rsxr_tone_register = g_register.Entry();
    g_rsxr_tone_destroy = reinterpret_cast<uintptr_t>(&DestroyRecords);
    g_rsxr_tone_manager = reinterpret_cast<uintptr_t>(&manager);
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &key;
    definition.members["Key"] = &key;
    collection[4] = tonetest::Addr(&node);
    collection[5] = collection[4] + 4;
    session[0x78 / 4] = song;
    std::memcpy(reinterpret_cast<uint8_t*>(session.data()) + 0x68, id.data(), sizeof id);
    context.esp = tonetest::Addr(stack);
    stack[5] = address;
    rsxrtones::Capture(tonetest::Addr(&attributes), tonetest::Addr(collection), id);
    RsxrTones_SongLoaded(song, false);
    Rsxr_Mid_ToneRegisterAfter(context);
    CHECK_EQ(g_calls.count, uint32_t(0));
    RsxrTones_SongLoaded(song, true);
    Rsxr_Mid_ToneRegisterAfter(context);
    Rsxr_Mid_ToneRegisterAfter(context);
    CHECK_EQ(g_calls.count, uint32_t(1));
    CHECK_EQ(definition.refs, uint32_t(1));
    RsxrTones_SongLoaded(song, true);
    rsxrtones::PreRegister(address);
    CHECK_EQ(g_calls.count, uint32_t(2));
    CHECK_EQ(g_calls.destroyed, uint32_t(1));
    g_rsxr_tones = 0;
    rsxrtones::PreRegister(address);
    CHECK_EQ(g_calls.count, uint32_t(2));
    CHECK_EQ(g_calls.destroyed, uint32_t(2));
    rsxrtones::Capture(0, 0, id);
    rsxrtones::PreRegister(address);
    CHECK_EQ(definition.refs, uint32_t(0));
    RsxrTones_SongLoaded(song, false);
}
