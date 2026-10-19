#include "features/testdrive/testdrive.h"
#include "test.h"
#include <cstring>

namespace {
// A state object as 0x766580 reads it: the name string at +8Ch (text or heap pointer, +10h end, +14h inline mark).
struct FakeState {
    uint8_t bytes[0xC0] = {};

    void SetInline(const char* name) {
        uint8_t* s = bytes + kUiStateName;

        std::memcpy(s, name, std::strlen(name));
        Put(s + 0x10, uint32_t(uintptr_t(s + std::strlen(name))));
        Put(s + 0x14, uint32_t(uintptr_t(s + 0x10)));
    }

    void SetHeap(const char* name) {
        uint8_t* s = bytes + kUiStateName;

        Put(s, uint32_t(uintptr_t(name)));
        Put(s + 0x10, uint32_t(uintptr_t(name + std::strlen(name))));
        Put(s + 0x14, uint32_t(uintptr_t(name)));  // anything but &+10h
    }

    static void Put(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
};

// global -> manager -> +28h state
struct FakeManager {
    uint8_t bytes[0x30] = {};
    uint32_t global = 0;

    explicit FakeManager(FakeState* state) {
        FakeState::Put(bytes + kUiStateCurrent, uint32_t(uintptr_t(state)));
        global = uint32_t(uintptr_t(bytes));
    }

    uintptr_t Global() const { return uintptr_t(&global); }
};
}  // namespace

TEST(uistate_reads_an_inline_name) {
    FakeState state;
    FakeManager manager(&state);
    char out[64];

    state.SetInline("MainMenu");
    CHECK(Rs7Ui_ReadState(manager.Global(), out, sizeof out));
    CHECK(!std::strcmp(out, "MainMenu"));
}

TEST(uistate_reads_a_heap_name) {
    static const char kName[] = "LearnASong_PreSongTuner";
    FakeState state;
    FakeManager manager(&state);
    char out[64];

    state.SetHeap(kName);
    CHECK(Rs7Ui_ReadState(manager.Global(), out, sizeof out));
    CHECK(!std::strcmp(out, kName));
}

TEST(uistate_refuses_missing_state_and_bad_names) {
    FakeState state;
    FakeManager manager(&state);
    char out[64];
    const uint32_t noManager = 0;

    CHECK(!Rs7Ui_ReadState(uintptr_t(&noManager), out, sizeof out));
    CHECK_EQ(out[0], 0);
    CHECK(!Rs7Ui_ReadState(manager.Global(), out, sizeof out));  // empty name
    state.SetInline("Main\x01Menu");
    CHECK(!Rs7Ui_ReadState(manager.Global(), out, sizeof out));
    state.SetInline("LongerThanTheBuffer");
    CHECK(!Rs7Ui_ReadState(manager.Global(), out, 8));
}
