#pragma once
#include "core/freeze.h"
#include "hooks/registry.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct Rs7ResolveResult;

// A failing site disables only its own feature group (registry.h Rs7_FeatureOf), never the whole DLL.
// Only a Core site failure still aborts patching.
//
// Resolve stage: sites that did not resolve are attributed to their group and the group is disabled.
// Returns false if one of them is Core (or has no hook); res.sites then holds the resolved sites.
bool Rs7_IsolateResolveFailures(Rs7ResolveResult& res, const std::function<void(const std::string&)>& log);

struct Rs7PatchOps {
    size_t count = 0;
    std::function<Rs7Feature(size_t)> feature;
    std::function<const char*(size_t)> name;
    std::function<bool(size_t)> should;   // re-evaluated every round (disabled groups drop out)
    std::function<bool(size_t)> prepare;  // must be idempotent: rounds repeat it
    std::function<bool(size_t)> apply;
    // Undo apply. Review #8: true only if the original bytes are back (read-back compare). A false result is
    // UNRECOVERABLE: no more rounds, the site stays in `written`, its feature group is disabled.
    std::function<bool(size_t)> restore;
    std::function<void(const std::string&)> log;
    // Review #9 (optional): the bytes apply/restore write for site i. When set, each round's writes and
    // its rollback run inside ONE thread freeze (core/freeze.h): apply and restore must then not allocate,
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
Rs7PatchResult Rs7_PatchIsolated(const Rs7PatchOps& ops, std::vector<size_t>& written);
