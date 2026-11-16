#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/protect.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

extern "C" void __cdecl Rs7_Mid_GKB(Rs7Mid::Context& c);
extern "C" void __cdecl Rs7_Mid_GKM(Rs7Mid::Context& c);
extern "C" void __cdecl Rs7_Mid_GKC(Rs7Mid::Context& c);
extern "C" void __cdecl Rs7_Mid_GKT(Rs7Mid::Context& c);
extern "C" uintptr_t g_rs7_tuner_bassbase;

namespace {
int g_bassLogThrows = 0;

void ThrowBassLog(const char* msg) {
    if (std::strstr(msg, "tuner bass open MIDI table")) {
        ++g_bassLogThrows;
        throw 1;
    }
}

struct ThrowingBassLog {
    ThrowingBassLog() {
        g_bassLogThrows = 0;
        Rs7Log_SetSink(ThrowBassLog);
    }

    ~ThrowingBassLog() { Rs7Log_SetSink(nullptr); }
};

void CallBass(Rs7Mid::Context& c) {
    Rs7_Mid_GKM(c);  // announce this load so only the init diagnostics can throw
    const LPTHREAD_START_ROUTINE run = [](LPVOID p) -> DWORD {
        try {
            Rs7_Mid_GKB(*static_cast<Rs7Mid::Context*>(p));
        } catch (...) {
            return 1;
        }

        return 0;
    };
    const HANDLE t = rs7test::Must(CreateThread(nullptr, 0, run, &c, 0, nullptr), "CreateThread");

    if (WaitForSingleObject(t, 5000) != WAIT_OBJECT_0) {
        std::fprintf(stderr, "FATAL: bass profile load blocked after a throwing log\n");
        std::abort();
    }

    DWORD code = 1;

    CHECK(GetExitCodeThread(t, &code));
    CHECK_EQ(code, 0u);
    CloseHandle(t);
}
}  // namespace

TEST(clite_bass_base_retries_after_protect_and_log_failures_without_blocking) {
    rs7test::Restore base(g_rs7_tuner_bassbase), bass(g_rs7_clite_bass[0]), mgr(g_rs7_clite_mgr);
    int32_t table[6] = {28, 33, 38, 43, 23, 0};  // a partial table must still initialise high C
    uint8_t profile[0x40] = {};
    uint32_t frame[4] = {};
    Rs7Mid::Context c{};

    c.ebp = uintptr_t(frame);
    c.esi = uintptr_t(profile);
    Rs7_Mid_GKM(c);
    g_rs7_clite_bass[0] = 6;
    g_rs7_tuner_bassbase = 0;
    Rs7_Mid_GKB(c);
    CHECK_EQ(profile[rs7clite::kProfileCount], 4);
    g_rs7_tuner_bassbase = uintptr_t(table);
    ThrowingBassLog log;

    Rs7_ProtectInjectFailure(uintptr_t(table + 4), uintptr_t(table + 6));
    CallBass(c);
    Rs7_ProtectInjectFailure(0, 0);
    CHECK_EQ(profile[rs7clite::kProfileCount], 4);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(profile + 0x3A), 0x19180004u);
    CHECK_EQ(table[5], 0);
    CallBass(c);
    CHECK_EQ(profile[rs7clite::kProfileCount], 6);
    CHECK_EQ(table[4], 23);
    CHECK_EQ(table[5], 48);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(profile + 0x3A), 0x19180006u);
    CallBass(c);
    CHECK_EQ(profile[rs7clite::kProfileCount], 6);
    CHECK_EQ(g_bassLogThrows, 2);  // protect-failure and success diagnostics both threw
    rs7colour::SetBassShift(0, false);
}

namespace {
struct Node {
    void** vt;
    uint8_t number[16] = {};
};

struct Tuning {
    void** vt;
    Node* node;
    unsigned keys;
};

void* __fastcall Find(void* self, void*, const char* key) {
    auto* obj = static_cast<Tuning*>(self);
    const char* names[] = {"Rs7String4", "Rs7String5", "Rs7String6", "Rs7String7"};

    for (unsigned i = 0; i < 4; ++i) {
        if ((obj->keys & (1u << i)) && std::strcmp(names[i], key) == 0) {
            return obj->node;
        }
    }

    return nullptr;
}

uint8_t* __fastcall Number(void* self, void*) {
    return static_cast<Node*>(self)->number;
}
}  // namespace

TEST(clite_keeps_pending_guitar_and_bass_counts_before_the_manager) {
    rs7test::Restore clite(g_rs7_clite), mgr(g_rs7_clite_mgr), base(g_rs7_tuner_bassbase);
    rs7test::Restore d0(g_rs7_clite_desired[0]), d1(g_rs7_clite_desired[1]);
    rs7test::Restore b0(g_rs7_clite_bass[0]), b1(g_rs7_clite_bass[1]);
    // GKT reads the game's global; only this synthetic page is mapped, in the test process.
    void* page = VirtualAlloc(reinterpret_cast<void*>(rs7clite::kTunerMgrGlobal & ~0xFFFFu), 0x10000,
                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!page) {
        rs7test::Skip("synthetic tuner manager address unavailable");
        return;
    }

    g_rs7_clite = 1;
    int32_t table[6] = {28, 33, 38, 43, 23, 48};

    g_rs7_tuner_bassbase = uintptr_t(table);
    void *objectVt[6] = {}, *nodeVt[21] = {};

    objectVt[5] = reinterpret_cast<void*>(&Find);
    nodeVt[20] = reinterpret_cast<void*>(&Number);
    Node node{nodeVt};
    Tuning tuning{objectVt, &node, 0};
    uint32_t parent[2] = {}, frame[4] = {uintptr_t(parent), rs7clite::kRetSetFromManifest, uintptr_t(&tuning), 0};
    uint8_t profile[0x40] = {};

    for (unsigned player : {0u, 1u}) {
        for (unsigned keys : {4u, 12u, 1u, 3u}) {  // guitar 7/8, bass 5/6
            g_rs7_clite_mgr = 0;
            g_rs7_clite_desired[player] = 6;
            g_rs7_clite_bass[player] = 4;
            tuning.keys = keys;
            frame[3] = player;
            Rs7Mid::Context c{};

            c.esi = player;
            c.ebp = uintptr_t(frame);
            Rs7_Mid_GKT(c);
            const unsigned guitar = keys == 4 ? 7 : keys == 12 ? 8 : 6;
            const unsigned bass = keys == 1 ? 5 : keys == 3 ? 6 : 4;

            CHECK_EQ(g_rs7_clite_desired[player], guitar);
            CHECK_EQ(g_rs7_clite_bass[player], bass);
            Rs7_Mid_GKM(c);
            c.esi = uintptr_t(profile);
            Rs7_Mid_GKC(c);
            CHECK_EQ(profile[rs7clite::kProfileCount], guitar);
            Rs7_Mid_GKB(c);
            CHECK_EQ(profile[rs7clite::kProfileCount], bass);
        }
    }

    VirtualFree(page, 0, MEM_RELEASE);
    rs7colour::SetBassShift(0, false);
    rs7colour::SetBassShift(1, false);
}
