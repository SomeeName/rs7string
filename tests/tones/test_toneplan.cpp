#include "features/tones/game.h"
#include "features/tones/toneplan.h"
#include "features/tones/tones.h"
#include "json_fixture.h"
#include "model/gamestring.h"
#include "test.h"
#include "testutil.h"
#include <array>

extern "C" uintptr_t g_rsxr_tone_context;
extern "C" uintptr_t g_rsxr_resume_TonePlanField, g_rsxr_resume_TonePlanExtension, g_rsxr_resume_TonePlanBound;
extern "C" void Rsxr_Mid_TonePlanBefore(RsxrMid::Context&);
extern "C" void Rsxr_Mid_TonePlanAfter(RsxrMid::Context&);
extern "C" void Rsxr_Mid_TonePlanField(RsxrMid::Context&);
extern "C" void Rsxr_Mid_TonePlanExtension(RsxrMid::Context&);
extern "C" void Rsxr_Mid_TonePlanBound(RsxrMid::Context&);

TEST(toneplan_uses_only_marked_extra_arrangements_and_preserves_sparse_letters) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones);
    tonetest::Node attributes, marker, base("clean"), echo("same"), zulu("same");

    g_rsxr_tones = 1;
    attributes.members["Tone_Base"] = &base;
    attributes.members["Tone_E"] = &echo;
    attributes.members["Tone_Z"] = &zulu;
    CHECK_EQ(rsxrtones::PlanSlots(tonetest::Addr(&attributes)), 0u);
    attributes.members["RsxrToneSlots"] = &marker;
    CHECK_EQ(rsxrtones::PlanSlots(tonetest::Addr(&attributes)), (1u << 26) | (1u << 5) | 1u);
    attributes.members.erase("Tone_E");
    attributes.members.erase("Tone_Z");
    CHECK_EQ(rsxrtones::PlanSlots(tonetest::Addr(&attributes)), 0u);
    attributes.members["Tone_Z"] = &zulu;
    marker.number = 25;
    CHECK_EQ(rsxrtones::PlanSlots(tonetest::Addr(&attributes)), 0u);
    marker.number = 26;
    g_rsxr_tones = 0;
    CHECK_EQ(rsxrtones::PlanSlots(tonetest::Addr(&attributes)), 0u);
}

TEST(toneplan_name_views_have_the_game_string_layout_for_all_27_slots) {
    for (uint32_t slot = 0; slot < 27; ++slot) {
        char extension[16]{}, field[16]{};

        CHECK(rsxr::ReadGameString(reinterpret_cast<void*>(uintptr_t(rsxrtones::PlanName(slot, false))), extension,
                                   sizeof extension));
        CHECK(rsxr::ReadGameString(reinterpret_cast<void*>(uintptr_t(rsxrtones::PlanName(slot, true))), field,
                                   sizeof field));
        CHECK(std::strcmp(extension, rsxrtones::SlotExtension(slot)) == 0);
        CHECK(std::strncmp(field, "Tone_", 5) == 0);

        if (slot) {
            CHECK_EQ(extension[4], static_cast<char>('A' + slot - 1));
            CHECK_EQ(field[5], static_cast<char>('A' + slot - 1));
        }
    }

    CHECK_EQ(rsxrtones::PlanName(27, true), 0u);
    CHECK(rsxrtones::SlotExtension(27) == nullptr);
    CHECK(std::strcmp(rsxrtones::SlotExtension(0), "ToneBase") == 0);
}

TEST(toneplan_wrap_redirects_both_sources_and_bound_and_restores_nested_stock_calls) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tones), restoreField(g_rsxr_resume_TonePlanField),
        restoreExtension(g_rsxr_resume_TonePlanExtension), restoreBound(g_rsxr_resume_TonePlanBound);
    tonetest::Node attributes, marker, echo("echo");
    uint32_t frame[7]{};
    RsxrMid::Context c{};

    g_rsxr_tones = 1;
    g_rsxr_resume_TonePlanField = 0x649D19;
    g_rsxr_resume_TonePlanExtension = 0x649E67;
    g_rsxr_resume_TonePlanBound = 0x64AB6D;
    attributes.members["RsxrToneSlots"] = &marker;
    attributes.members["Tone_E"] = &echo;
    c.esp = tonetest::Addr(frame);
    frame[6] = tonetest::Addr(&attributes);
    Rsxr_Mid_TonePlanBefore(c);
    CHECK(rsxrtones::PlanActive());

    for (uint32_t slot : {0u, 5u, 26u}) {
        c.eax = slot * 0x18;
        Rsxr_Mid_TonePlanField(c);
        CHECK_EQ(c.edi, rsxrtones::PlanName(slot, true));
        CHECK_EQ(c.jump, uint32_t(g_rsxr_resume_TonePlanField));
        Rsxr_Mid_TonePlanExtension(c);
        CHECK_EQ(c.eax, rsxrtones::PlanName(slot, false));
        CHECK_EQ(c.jump, uint32_t(g_rsxr_resume_TonePlanExtension));
    }

    c.eax = 26;
    Rsxr_Mid_TonePlanBound(c);
    CHECK_EQ(c.jump, 0x649CA5u);
    CHECK(c.eflags & 1u);
    c.eax = 27;
    Rsxr_Mid_TonePlanBound(c);
    CHECK_EQ(c.jump, 0x64AB6Du);
    CHECK(c.eflags & 0x40u);
    const uint32_t outer = rsxrtones::BeginPlan(0);

    CHECK(!rsxrtones::PlanActive());
    c.eax = 5;
    c.jump = 123;
    c.eflags = 456;
    Rsxr_Mid_TonePlanBound(c);
    CHECK_EQ(c.jump, 123u);
    CHECK_EQ(c.eflags, 456u);
    rsxrtones::EndPlan(outer);
    CHECK(rsxrtones::PlanActive());
    Rsxr_Mid_TonePlanAfter(c);
    CHECK(!rsxrtones::PlanActive());
}

TEST(toneplan_override_reads_the_stock_player_context_and_retains_the_object) {
    rsxrtest::Restore<uintptr_t> restore(g_rsxr_tone_context);
    tonetest::Context context;
    tonetest::Node echo, zulu;

    g_rsxr_tone_context = context.Global();
    context.players[0].members["LAS_ToneCustomData_ToneE"] = &echo;
    context.players[1].members["LAS_ToneCustomData_ToneZ"] = &zulu;
    CHECK_EQ(rsxrtones::Override(0, 5), tonetest::Addr(&echo));
    CHECK_EQ(echo.refs, 1u);
    CHECK(rsxrtones::Reference(tonetest::Addr(&echo), false));
    CHECK_EQ(rsxrtones::Override(1, 26), tonetest::Addr(&zulu));
    CHECK(rsxrtones::Reference(tonetest::Addr(&zulu), false));
    CHECK_EQ(rsxrtones::Override(1, 5), 0u);
    CHECK_EQ(rsxrtones::Override(2, 26), 0u);
    context.players[0].members.clear();
    CHECK_EQ(rsxrtones::Override(0, 5), 0u);
    CHECK_EQ(echo.refs, 0u);
    CHECK_EQ(zulu.refs, 0u);
}
