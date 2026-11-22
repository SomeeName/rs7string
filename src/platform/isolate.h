#pragma once
#include "platform/freeze.h"
#include "platform/hooktable.h"
#include "platform/patch.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct RsxrResolveResult;

// A failing site disables only its own feature group, never the whole DLL. Which groups exist, which site
// belongs to which and what disabling one does is the app's (kRsxrGroups, src/hook_rules.h); a group that cannot be
// isolated (the core, or a member of a unit without fallback) still aborts patching.
struct RsxrGroupPolicy {
    size_t count;                         // number of groups: bounds the patch rounds (one group off per round)
    RsxrGroup (*ofSite)(const char* id);  // a site's group; the core group for an id without a hook
    bool (*isUnit)(RsxrGroup);            // a member of a unit without fallback: its failure aborts like the core's
    bool (*disable)(RsxrGroup);           // marks the group failed; false (nothing changed) for the core group
    bool (*disabled)(RsxrGroup);          // whether the group has failed
    const char* (*name)(RsxrGroup);
};

// Resolve stage: sites that did not resolve are attributed to their group and the group is disabled.
// Returns false if one of them cannot be isolated; res.sites then holds the resolved sites.
bool Rsxr_IsolateResolveFailures(RsxrResolveResult& res, const RsxrGroupPolicy& groups,
                                 const std::function<void(const std::string&)>& log);

struct RsxrPatchOps {
    size_t count = 0;
    std::function<RsxrGroup(size_t)> group;
    std::function<const char*(size_t)> name;
    std::function<bool(size_t)> should;   // re-evaluated every round (disabled groups drop out)
    std::function<bool(size_t)> prepare;  // must be idempotent: rounds repeat it
    std::function<RsxrWriteResult(size_t)> apply;
    // Undo apply: true only if the original bytes are back (read-back compare). A false result is
    // UNRECOVERABLE: no more rounds, the site stays in `written`, its feature group is disabled.
    // An apply protection-cleanup failure is also UNRECOVERABLE even when its bytes can be put back.
    std::function<bool(size_t)> restore;
    std::function<void(const std::string&)> log;
    // Optional: the bytes apply/restore write for site i. When set, each round's writes and
    // its rollback run inside ONE thread freeze (platform/freeze.h): apply and restore must then not allocate,
    // log or take locks (prepare does that work). Unset (unit tests of the policy): no freeze.
    std::function<RsxrRange(size_t)> range;
    // Optional precondition, after a successful freeze and before ANY write in each round. Same no-lock,
    // no-allocation rules as apply. False aborts the whole batch without writing or retrying the round.
    std::function<bool()> beforeWrite;
    unsigned freezeDeadlineMs = 2000;
    // A freeze that times out (thread control kept stalling, a thread stayed in a range, the reader was still
    // blocked) is tried again after freezeRetryMs, freezeAttempts times in all, before the round fails.
    unsigned freezeAttempts = 3;
    unsigned freezeRetryMs = 1000;
};
enum class RsxrPatchResult {
    Ok,                  // `written` lists the written indices
    CoreFailed,          // a Core site failed; everything restored
    PreconditionFailed,  // beforeWrite refused the batch; nothing written in that round
    FreezeTimeout,       // a thread never left a site range before the deadline; nothing written in that round
    Unrecoverable,       // a rollback or protection cleanup failed: `written` lists unresolved sites
};
// Prepare all, then write all; on a failure everything written in that round is restored, the failing
// site's group is disabled and the round restarts.
RsxrPatchResult Rsxr_PatchIsolated(const RsxrGroupPolicy& groups, const RsxrPatchOps& ops,
                                   std::vector<size_t>& written);
