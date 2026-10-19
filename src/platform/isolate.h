#pragma once
#include "platform/freeze.h"
#include "platform/hooktable.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct Rs7ResolveResult;

// A failing site disables only its own feature group, never the whole DLL. Which groups exist, which site
// belongs to which and what disabling one does is the app's (kRs7Groups, src/hook_rules.h); a group that cannot be
// isolated (the core, or a member of a unit without fallback) still aborts patching.
struct Rs7GroupPolicy {
    size_t count;                        // number of groups: bounds the patch rounds (one group off per round)
    Rs7Group (*ofSite)(const char* id);  // a site's group; the core group for an id without a hook
    bool (*isUnit)(Rs7Group);            // a member of a unit without fallback: its failure aborts like the core's
    bool (*disable)(Rs7Group);           // marks the group failed; false (nothing changed) for the core group
    bool (*disabled)(Rs7Group);          // whether the group has failed
    const char* (*name)(Rs7Group);
};

// Resolve stage: sites that did not resolve are attributed to their group and the group is disabled.
// Returns false if one of them cannot be isolated; res.sites then holds the resolved sites.
bool Rs7_IsolateResolveFailures(Rs7ResolveResult& res, const Rs7GroupPolicy& groups,
                                const std::function<void(const std::string&)>& log);

struct Rs7PatchOps {
    size_t count = 0;
    std::function<Rs7Group(size_t)> group;
    std::function<const char*(size_t)> name;
    std::function<bool(size_t)> should;   // re-evaluated every round (disabled groups drop out)
    std::function<bool(size_t)> prepare;  // must be idempotent: rounds repeat it
    std::function<bool(size_t)> apply;
    // Undo apply. Review #8: true only if the original bytes are back (read-back compare). A false result is
    // UNRECOVERABLE: no more rounds, the site stays in `written`, its feature group is disabled.
    std::function<bool(size_t)> restore;
    std::function<void(const std::string&)> log;
    // Review #9 (optional): the bytes apply/restore write for site i. When set, each round's writes and
    // its rollback run inside ONE thread freeze (platform/freeze.h): apply and restore must then not allocate,
    // log or take locks (prepare does that work). Unset (unit tests of the policy): no freeze.
    std::function<Rs7Range(size_t)> range;
    unsigned freezeDeadlineMs = 2000;
};
enum class Rs7PatchResult {
    Ok,             // `written` lists the written indices
    CoreFailed,     // a Core site failed; everything restored
    FreezeTimeout,  // a thread never left a site range before the deadline; nothing written in that round
    Unrecoverable,  // a rollback failed: `written` lists the sites that could not be restored (still ours)
};
// Prepare all, then write all; on a failure everything written in that round is restored, the failing
// site's group is disabled and the round restarts.
Rs7PatchResult Rs7_PatchIsolated(const Rs7GroupPolicy& groups, const Rs7PatchOps& ops, std::vector<size_t>& written);
