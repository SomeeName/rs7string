#include "features/diagrams/diagrams.h"
#include "hook_rules.h"
#include "platform/isolate.h"
#include "platform/patch.h"
#include "platform/protect.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

// Review #8: a rollback reports success only after a read-back compare; a failed one is UNRECOVERABLE.
namespace {
struct Page {
    uint8_t* p;

    Page() {
        p = static_cast<uint8_t*>(
            rsxrtest::Must(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), "VirtualAlloc"));
        std::memset(p, 0xCC, 4096);
        DWORD old;

        VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old);  // like game code
    }

    ~Page() {
        Rsxr_ProtectInjectFailure(0, 0);
        VirtualFree(p, 0, MEM_RELEASE);
    }

    uintptr_t At(size_t off) const { return reinterpret_cast<uintptr_t>(p + off); }
};

const uint8_t kOrig[5] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
const uint8_t kOurs[5] = {0xE9, 1, 2, 3, 4};
}  // namespace

TEST(restore_bytes_verifies_by_read_back) {
    Page pg;

    CHECK(Rsxr_WriteBytes(pg.At(0x10), kOurs, 5));
    CHECK(Rsxr_RestoreBytes(pg.At(0x10), kOrig, 5));
    CHECK(std::memcmp(pg.p + 0x10, kOrig, 5) == 0);
    CHECK(Rsxr_WriteBytes(pg.At(0x10), kOurs, 5));
    Rsxr_ProtectInjectFailure(pg.At(0), pg.At(0x1000));  // VirtualProtect fails from here on
    CHECK(!Rsxr_RestoreBytes(pg.At(0x10), kOrig, 5));
    CHECK(std::memcmp(pg.p + 0x10, kOurs, 5) == 0);  // nothing written, and that is reported
    Rsxr_ProtectInjectFailure(0, 0);
    // A protect "failure" that leaves the right bytes in place is still a success (the bytes decide).
    CHECK(Rsxr_RestoreBytes(pg.At(0x10), kOrig, 5));
    Rsxr_ProtectInjectFailure(pg.At(0), pg.At(0x1000));
    CHECK(Rsxr_RestoreBytes(pg.At(0x10), kOrig, 5));
}

namespace {
struct Batch {
    Page pg;
    std::vector<std::string> log;
    int applies = 0, restores = 0;
    uintptr_t failApplyAt = 0, failRestoreAt = 0;  // site whose protect fails at write / at rollback

    RsxrPatchOps Ops(bool frozen) {
        RsxrPatchOps o;

        o.count = 3;  // 0: Core, 1: Diagrams, 2: Diagrams
        o.group = [](size_t i) {
            return Rsxr_Group(i == 0 ? RsxrFeature::Core : RsxrFeature::Diagrams);
        };
        o.name = [](size_t i) {
            static const char* n[3] = {"S0", "S1", "S2"};

            return n[i];
        };
        o.should = [](size_t i) {
            return i == 0 || !Rsxr_FeatureDisabled(RsxrFeature::Diagrams);
        };
        o.prepare = [](size_t) {
            return true;
        };
        o.apply = [this](size_t i) {
            ++applies;
            Rsxr_ProtectInjectFailure(failApplyAt == Site(i) ? Site(i) : 0, failApplyAt == Site(i) ? Site(i) + 5 : 0);
            const bool ok = Rsxr_WriteBytes(Site(i), kOurs, 5);

            Rsxr_ProtectInjectFailure(0, 0);

            return ok;
        };
        o.restore = [this](size_t i) {
            ++restores;
            Rsxr_ProtectInjectFailure(failRestoreAt == Site(i) ? Site(i) : 0,
                                      failRestoreAt == Site(i) ? Site(i) + 5 : 0);
            const bool ok = Rsxr_RestoreBytes(Site(i), kOrig, 5);

            Rsxr_ProtectInjectFailure(0, 0);

            return ok;
        };

        if (frozen) {
            o.range = [this](size_t i) {
                return RsxrRange{Site(i), Site(i) + 5};
            };
        }

        o.log = [this](const std::string& m) {
            log.push_back(m);
        };

        return o;
    }

    uintptr_t Site(size_t i) const { return pg.At(0x100 * (i + 1)); }

    bool Logged(const char* what) const {
        for (const auto& l : log) {
            if (l.find(what) != std::string::npos) {
                return true;
            }
        }

        return false;
    }
};

void UnrecoverableCase(bool frozen) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    Batch b;

    b.failApplyAt = b.Site(2);    // S2 cannot be written ...
    b.failRestoreAt = b.Site(1);  // ... and S1 cannot be put back
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, b.Ops(frozen), written) == RsxrPatchResult::Unrecoverable);
    CHECK_EQ(b.applies, 3);  // one round only: no retry over an unrestored site
    CHECK_EQ(b.restores, 2);
    CHECK_EQ(written.size(), size_t(1));  // S1 is still ours, and recorded as such
    if (!written.empty()) {
        CHECK_EQ(written[0], size_t(1));
    }
    CHECK(std::memcmp(reinterpret_cast<void*>(b.Site(1)), kOurs, 5) == 0);
    CHECK(std::memcmp(reinterpret_cast<void*>(b.Site(0)), kOrig, 5) == 0);  // S0 restored (verified)
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Diagrams));
    CHECK(b.Logged("UNRECOVERABLE S1"));
    CHECK(!b.Logged("restored\""));
    Rsxr_ResetFeatureFailures();
}
}  // namespace

TEST(isolate_failed_rollback_is_unrecoverable_and_never_retried) {
    UnrecoverableCase(false);
}

TEST(isolate_failed_rollback_is_unrecoverable_under_the_freeze) {
    UnrecoverableCase(true);
}

TEST(isolate_verified_rollback_still_isolates_and_retries) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    Batch b;

    b.failApplyAt = b.Site(2);  // S2 cannot be written; the rollback of S0/S1 succeeds
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, b.Ops(true), written) == RsxrPatchResult::Ok);
    CHECK_EQ(written.size(), size_t(1));  // round 2: S0 only (Diagrams disabled)
    CHECK(std::memcmp(reinterpret_cast<void*>(b.Site(1)), kOrig, 5) == 0);
    CHECK(!b.Logged("UNRECOVERABLE"));
    Rsxr_ResetFeatureFailures();
}

TEST(isolate_cleanup_failure_keeps_the_written_site_accounted_for_and_stops_rounds) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    Batch b;
    RsxrPatchOps ops = b.Ops(true);

    ops.apply = [&](size_t i) {
        ++b.applies;
        Rsxr_ProtectInjectFailure(i == 1 ? b.Site(i) : 0, i == 1 ? b.Site(i) + 5 : 0, PAGE_EXECUTE_READ);
        const RsxrWriteResult result = Rsxr_WriteBytes(b.Site(i), kOurs, 5);

        Rsxr_ProtectInjectFailure(0, 0);

        return result;
    };
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, ops, written) == RsxrPatchResult::Unrecoverable);
    CHECK_EQ(b.applies, 2);
    CHECK_EQ(b.restores, 2);  // includes the site written before its cleanup failed
    CHECK_EQ(written.size(), size_t(1));
    if (!written.empty()) {
        CHECK_EQ(written[0], size_t(1));
    }
    CHECK(std::memcmp(reinterpret_cast<void*>(b.Site(0)), kOrig, 5) == 0);
    CHECK(std::memcmp(reinterpret_cast<void*>(b.Site(1)), kOrig, 5) == 0);
    CHECK(b.Logged("page protection failed"));
    Rsxr_ResetFeatureFailures();
}
