#include "core/assetdump.h"
#include "test.h"
#include <stdexcept>

namespace {
struct DumpFault {
    int cleaned = 0, unwound = 0;
};

void ThrowDump(void* context) {
    struct Scope {
        DumpFault* fault;

        ~Scope() { ++fault->unwound; }
    } scope{static_cast<DumpFault*>(context)};

    throw std::runtime_error("injected dump allocation/logging failure");
}

void CleanDump(void* context) {
    ++static_cast<DumpFault*>(context)->cleaned;
}
}  // namespace

TEST(assetdump_worker_contains_cpp_failure_and_cleans_job) {
    DumpFault fault;
    bool escaped = false, ok = true;

    try {
        ok = Rs7AssetDump_RunWorker(ThrowDump, &fault, CleanDump);
    } catch (...) {
        escaped = true;
    }

    CHECK(!escaped);
    CHECK(!ok);
    CHECK(fault.unwound == 1);
    CHECK(fault.cleaned == 1);
    CHECK(Rs7AssetDump_Status().state == 4);
}

TEST(assetdump_safe_rel_path_maps_slashes) {
    CHECK(Rs7AssetDump_SafeRelPath("gfxassets/headstock/peg.nif") == L"gfxassets\\headstock\\peg.nif");
    CHECK(Rs7AssetDump_SafeRelPath("manifest.json") == L"manifest.json");
}

TEST(assetdump_safe_rel_path_rejects_unsafe_names) {
    CHECK(Rs7AssetDump_SafeRelPath(nullptr).empty());
    CHECK(Rs7AssetDump_SafeRelPath("").empty());
    CHECK(Rs7AssetDump_SafeRelPath("/abs/x.nif").empty());
    CHECK(Rs7AssetDump_SafeRelPath("c:/x.nif").empty());
    CHECK(Rs7AssetDump_SafeRelPath("a/../../x.nif").empty());
    CHECK(Rs7AssetDump_SafeRelPath("a//x.nif").empty());
    CHECK(Rs7AssetDump_SafeRelPath("a/x\n").empty());
}

TEST(assetdump_priority_puts_guitar_assets_first_and_dlc_last) {
    CHECK(Rs7AssetDump_Priority(L"base\\dependency_guitarstandard3x3peg.psarc") == 0);
    CHECK(Rs7AssetDump_Priority(L"BASE\\Dependency_GuitarCommon.psarc") == 0);
    CHECK(Rs7AssetDump_Priority(L"guitars.psarc") < Rs7AssetDump_Priority(L"cache.psarc"));
    CHECK(Rs7AssetDump_Priority(L"cache.psarc") < Rs7AssetDump_Priority(L"audio.psarc"));
    CHECK(Rs7AssetDump_Priority(L"audio.psarc") < Rs7AssetDump_Priority(L"dlc\\x_p.psarc"));
}

namespace {
void OkDump(void* context) {
    ++static_cast<DumpFault*>(context)->unwound;
}

void FaultDump(void*) {
    *static_cast<volatile int*>(nullptr) = 1;
}
}  // namespace

TEST(assetdump_worker_success_runs_cleanup_once) {
    DumpFault fault;

    CHECK(Rs7AssetDump_RunWorker(OkDump, &fault, CleanDump));
    CHECK(fault.unwound == 1);
    CHECK(fault.cleaned == 1);
}

TEST(assetdump_worker_contains_hardware_fault_and_cleans_job) {
    DumpFault fault;

    CHECK(!Rs7AssetDump_RunWorker(FaultDump, &fault, CleanDump));
    CHECK(fault.cleaned == 1);
    CHECK(Rs7AssetDump_Status().state == 4);
}
