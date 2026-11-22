#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/protect.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

extern "C" void __cdecl Rsxr_Mid_GKB(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_GKM(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_GKC(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_GKT(RsxrMid::Context& c);
extern "C" uintptr_t g_rsxr_tuner_bassbase;

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
        RsxrLog_SetSink(ThrowBassLog);
    }

    ~ThrowingBassLog() { RsxrLog_SetSink(nullptr); }
};

void CallBass(RsxrMid::Context& c) {
    Rsxr_Mid_GKM(c);  // announce this load so only the init diagnostics can throw
    const LPTHREAD_START_ROUTINE run = [](LPVOID p) -> DWORD {
        try {
            Rsxr_Mid_GKB(*static_cast<RsxrMid::Context*>(p));
        } catch (...) {
            return 1;
        }

        return 0;
    };
    const HANDLE t = rsxrtest::Must(CreateThread(nullptr, 0, run, &c, 0, nullptr), "CreateThread");

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
    rsxrtest::Restore base(g_rsxr_tuner_bassbase), bass(g_rsxr_clite_bass[0]), mgr(g_rsxr_clite_mgr);
    int32_t table[6] = {28, 33, 38, 43, 23, 0};  // a partial table must still initialise high C
    uint8_t profile[0x40] = {};
    uint32_t frame[4] = {};
    RsxrMid::Context c{};

    c.ebp = uintptr_t(frame);
    c.esi = uintptr_t(profile);
    Rsxr_Mid_GKM(c);
    g_rsxr_clite_bass[0] = 6;
    g_rsxr_tuner_bassbase = 0;
    Rsxr_Mid_GKB(c);
    CHECK_EQ(profile[rsxrclite::kProfileCount], 4);
    g_rsxr_tuner_bassbase = uintptr_t(table);
    ThrowingBassLog log;

    Rsxr_ProtectInjectFailure(uintptr_t(table + 4), uintptr_t(table + 6));
    CallBass(c);
    Rsxr_ProtectInjectFailure(0, 0);
    CHECK_EQ(profile[rsxrclite::kProfileCount], 4);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(profile + 0x3A), 0x19180004u);
    CHECK_EQ(table[5], 0);
    CallBass(c);
    CHECK_EQ(profile[rsxrclite::kProfileCount], 6);
    CHECK_EQ(table[4], 23);
    CHECK_EQ(table[5], 48);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(profile + 0x3A), 0x19180006u);
    CallBass(c);
    CHECK_EQ(profile[rsxrclite::kProfileCount], 6);
    CHECK_EQ(g_bassLogThrows, 2);  // protect-failure and success diagnostics both threw
    rsxrcolour::SetBassShift(0, false);
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
    const char* names[] = {"RsxrString4", "RsxrString5", "RsxrString6", "RsxrString7"};

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
    rsxrtest::Restore clite(g_rsxr_clite), mgr(g_rsxr_clite_mgr), base(g_rsxr_tuner_bassbase);
    rsxrtest::Restore d0(g_rsxr_clite_desired[0]), d1(g_rsxr_clite_desired[1]);
    rsxrtest::Restore b0(g_rsxr_clite_bass[0]), b1(g_rsxr_clite_bass[1]);
    // GKT reads the game's global; only this synthetic page is mapped, in the test process.
    void* page = VirtualAlloc(reinterpret_cast<void*>(rsxrclite::kTunerMgrGlobal & ~0xFFFFu), 0x10000,
                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!page) {
        rsxrtest::Skip("synthetic tuner manager address unavailable");
        return;
    }

    g_rsxr_clite = 1;
    int32_t table[6] = {28, 33, 38, 43, 23, 48};

    g_rsxr_tuner_bassbase = uintptr_t(table);
    void *objectVt[6] = {}, *nodeVt[21] = {};

    objectVt[5] = reinterpret_cast<void*>(&Find);
    nodeVt[20] = reinterpret_cast<void*>(&Number);
    Node node{nodeVt};
    Tuning tuning{objectVt, &node, 0};
    uint32_t parent[2] = {}, frame[4] = {uintptr_t(parent), rsxrclite::kRetSetFromManifest, uintptr_t(&tuning), 0};
    uint8_t profile[0x40] = {};

    for (unsigned player : {0u, 1u}) {
        for (unsigned keys : {4u, 12u, 1u, 3u}) {  // guitar 7/8, bass 5/6
            g_rsxr_clite_mgr = 0;
            g_rsxr_clite_desired[player] = 6;
            g_rsxr_clite_bass[player] = 4;
            tuning.keys = keys;
            frame[3] = player;
            RsxrMid::Context c{};

            c.esi = player;
            c.ebp = uintptr_t(frame);
            Rsxr_Mid_GKT(c);
            const unsigned guitar = keys == 4 ? 7 : keys == 12 ? 8 : 6;
            const unsigned bass = keys == 1 ? 5 : keys == 3 ? 6 : 4;

            CHECK_EQ(g_rsxr_clite_desired[player], guitar);
            CHECK_EQ(g_rsxr_clite_bass[player], bass);
            Rsxr_Mid_GKM(c);
            c.esi = uintptr_t(profile);
            Rsxr_Mid_GKC(c);
            CHECK_EQ(profile[rsxrclite::kProfileCount], guitar);
            Rsxr_Mid_GKB(c);
            CHECK_EQ(profile[rsxrclite::kProfileCount], bass);
        }
    }

    VirtualFree(page, 0, MEM_RELEASE);
    rsxrcolour::SetBassShift(0, false);
    rsxrcolour::SetBassShift(1, false);
}
