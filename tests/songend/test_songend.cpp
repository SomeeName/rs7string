#include "features/allocstats/allocstats.h"
#include "features/songend/songend.h"
#include "platform/log.h"
#include "test.h"
#include <cstring>
#include <string>

extern "C" void __cdecl Rsxr_Mid_SE2(RsxrMid::Context&);

namespace {
std::string g_snapshot;

void Capture(const char* line) {
    g_snapshot += line;
    g_snapshot += '\n';
}
}  // namespace

TEST(songend_allocation_snapshot_is_independent_of_the_end_log_switch) {
    const uintptr_t saved = g_rsxr_endlog;
    const uint32_t stack[2] = {0, 3};
    RsxrMid::Context context{};

    context.esp = uint32_t(uintptr_t(stack));
    g_rsxr_endlog = 0;
    RsxrAlloc_SetEnabledForTests(true);
    RsxrLog_SetSink(Capture);
    g_snapshot.clear();
    Rsxr_Mid_SE2(context);
    CHECK(g_snapshot.find("allocstats song end #") != std::string::npos);
    CHECK(g_snapshot.find("song end [SE2") == std::string::npos);
    RsxrAlloc_SetEnabledForTests(false);
    g_snapshot.clear();
    Rsxr_Mid_SE2(context);
    CHECK(g_snapshot.empty());
    RsxrLog_SetSink(nullptr);
    g_rsxr_endlog = saved;
}

TEST(songend_end_event_is_type_3_to_all_or_this_session) {
    CHECK(rsxrend::IsEndEvent(2, 3, 7));
    CHECK(rsxrend::IsEndEvent(7, 3, 7));
    CHECK(!rsxrend::IsEndEvent(8, 3, 7));
    CHECK(!rsxrend::IsEndEvent(2, 2, 7));
}

TEST(songend_new_state_at_the_known_callers) {
    CHECK_EQ(rsxrend::NewState(0x7E0A3C, 0xAAAA0004, 9), 4);  // UpdateState: DI = pending
    CHECK_EQ(rsxrend::NewState(0x7DD65E, 1, 1), 4);           // end-song event
    CHECK_EQ(rsxrend::NewState(0x7DF85A, 3, 9), 3);
    CHECK_EQ(rsxrend::NewState(0x7EF06C, 9, 2), 2);  // sync: BX
    CHECK_EQ(rsxrend::NewState(0x7EF0C8, 9, 5), 5);
    CHECK_EQ(rsxrend::NewState(0x401000, 3, 3), -1);
    CHECK(rsxrend::CallerName(0x401000)[0] == 0);
    CHECK(rsxrend::CallerName(0x7DD65E)[0] != 0);
}

TEST(songend_return_addresses_follow_a_call) {
    const uint8_t rel[7] = {0x90, 0x90, 0xE8, 1, 2, 3, 4};           // call rel32
    const uint8_t reg[7] = {0, 0, 0, 0, 0, 0xFF, 0xD0};              // call eax
    const uint8_t mem[7] = {0, 0, 0, 0, 0xFF, 0x50, 0x24};           // call [eax+24h]
    const uint8_t d32[7] = {0, 0xFF, 0x92, 0xB0, 0, 0, 0};           // call [edx+0B0h]
    const uint8_t iat[7] = {0, 0xFF, 0x15, 0xC0, 0xAB, 0x37, 0x02};  // call [0237ABC0h]
    const uint8_t none[7] = {0x8B, 0x45, 0x08, 0x33, 0xC0, 0x5D, 0xC3};
    const uint8_t jmp[7] = {0, 0, 0, 0, 0, 0xFF, 0xE0};  // jmp eax is no call

    CHECK(rsxrend::EndsInCall(rel));
    CHECK(rsxrend::EndsInCall(reg));
    CHECK(rsxrend::EndsInCall(mem));
    CHECK(rsxrend::EndsInCall(d32));
    CHECK(rsxrend::EndsInCall(iat));
    CHECK(!rsxrend::EndsInCall(none));
    CHECK(!rsxrend::EndsInCall(jmp));
}

TEST(songend_logger_is_on_by_default) {
    CHECK_EQ(g_rsxr_endlog, uintptr_t(1));
}
