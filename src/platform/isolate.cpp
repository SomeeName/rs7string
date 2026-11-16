#include "platform/isolate.h"
#include "platform/resolve.h"
#include <windows.h>

namespace {
enum class Verdict { Unit, Core, Disabled };

// The one isolation decision: a unit member or the core cannot be isolated; any other group is disabled.
Verdict Decide(const Rs7GroupPolicy& g, Rs7Group f) {
    if (g.isUnit(f)) {
        return Verdict::Unit;
    }

    return g.disable(f) ? Verdict::Disabled : Verdict::Core;
}

// Disables the group of a failing site; false if it is the core or a member of a unit without fallback.
bool Isolate(const Rs7GroupPolicy& g, const Rs7PatchOps& ops, size_t i, const char* stage) {
    const Rs7Group f = ops.group(i);
    const std::string head = std::string("site ") + ops.name(i) + " failed at " + stage;

    switch (Decide(g, f)) {
        case Verdict::Unit:
            ops.log(head + " and belongs to a unit without fallback (" + g.name(f) +
                    ") - nothing patched, the game stays stock");
            return false;
        case Verdict::Core:
            ops.log(head + " and belongs to the core - nothing patched");
            return false;
        case Verdict::Disabled:
            break;
    }

    ops.log(head + " -> feature " + g.name(f) + " DISABLED, everything else is patched");

    return true;
}
}  // namespace

bool Rs7_IsolateResolveFailures(Rs7ResolveResult& res, const Rs7GroupPolicy& groups,
                                const std::function<void(const std::string&)>& log) {
    res.sites = res.resolved;

    for (size_t k = 0; k < res.failed.size(); ++k) {
        const Rs7Group f = groups.ofSite(res.failed[k]->id);
        const std::string why = k < res.errors.size() ? res.errors[k] : std::string(res.failed[k]->id);

        switch (Decide(groups, f)) {
            case Verdict::Unit:
                log("site error (unit without fallback, " + std::string(groups.name(f)) + "): " + why);
                return false;
            case Verdict::Core:
                log("site error (core): " + why);
                return false;
            case Verdict::Disabled:
                break;
        }

        log("site error: " + why + " -> feature " + groups.name(f) + " DISABLED, everything else is patched");
    }

    res.ok = true;

    return true;
}

namespace {
// Runs inside the freeze: no allocation (written has capacity for every todo entry), no logging.
struct Round {
    const Rs7PatchOps* ops;
    const std::vector<size_t>* todo;
    std::vector<size_t>* written;
    size_t failedAt;
    std::vector<size_t>* unrestored;  // capacity reserved: restore failures
    bool refused = false;
};

void WriteRound(void* p) {
    Round& r = *static_cast<Round*>(p);

    r.failedAt = size_t(-1);
    r.refused = r.ops->beforeWrite && !r.ops->beforeWrite();
    if (r.refused) {
        return;
    }

    for (size_t k = 0; k < r.todo->size(); ++k) {
        const size_t i = (*r.todo)[k];

        const Rs7WriteResult result = r.ops->apply(i);

        if (result.written) {
            r.written->push_back(i);
        }
        if (result) {
            continue;
        }

        r.failedAt = k;

        for (size_t j : *r.written) {
            const bool restored = r.ops->restore(j);

            if (!restored || (j == i && !result.protectionRestored)) {
                r.unrestored->push_back(j);
            }
        }

        if (!result.written && !result.protectionRestored) {
            r.unrestored->push_back(i);
        }

        r.written->clear();
        return;
    }
}

// One round's writes under the thread freeze, tried ops.freezeAttempts times (a timed-out freeze writes nothing);
// false when every attempt timed out.
bool WriteRoundFrozen(const Rs7PatchOps& ops, const std::vector<size_t>& todo, Round& r, int round) {
    std::vector<Rs7Range> ranges;

    ranges.reserve(todo.size());

    for (size_t i : todo) {
        ranges.push_back(ops.range(i));
    }

    for (unsigned attempt = 1;; ++attempt) {
        Rs7FreezeStats st;
        const bool ran = Rs7_RunFrozen(ranges.data(), ranges.size(), &WriteRound, &r, &st, ops.freezeDeadlineMs);
        char line[512];

        Rs7_FreezeDescribe(st, line, sizeof line);
        std::string msg =
            std::string(line) + " (round " + std::to_string(round) + ", " + std::to_string(todo.size()) + " sites";

        if (st.timedOut && st.busyRange < todo.size()) {
            msg += std::string(", busy site ") + ops.name(todo[st.busyRange]);
        }
        if (attempt > 1 || !ran) {
            msg += ", attempt " + std::to_string(attempt) + " of " + std::to_string(ops.freezeAttempts);
        }
        ops.log(msg + ")");
        if (ran) {
            return true;
        }
        if (st.tooMany || attempt >= ops.freezeAttempts) {
            return false;
        }

        Sleep(ops.freezeRetryMs);
    }
}
}  // namespace

Rs7PatchResult Rs7_PatchIsolated(const Rs7GroupPolicy& groups, const Rs7PatchOps& ops, std::vector<size_t>& written) {
    for (int round = 0; round <= int(groups.count); ++round) {
        written.clear();
        std::vector<size_t> todo;

        for (size_t i = 0; i < ops.count; ++i) {
            if (ops.should(i)) {
                todo.push_back(i);
            }
        }

        bool restart = false;

        for (size_t i : todo) {
            if (!ops.prepare(i)) {
                if (!Isolate(groups, ops, i, "preparation")) {
                    return Rs7PatchResult::CoreFailed;
                }

                restart = true;
                break;
            }
        }

        if (restart) {
            continue;
        }

        written.reserve(todo.size());
        std::vector<size_t> unrestored;

        unrestored.reserve(todo.size());
        Round r{&ops, &todo, &written, size_t(-1), &unrestored};

        if (ops.range) {
            if (!WriteRoundFrozen(ops, todo, r, round)) {
                return Rs7PatchResult::FreezeTimeout;
            }
        } else {
            WriteRound(&r);
        }

        if (r.refused) {
            return Rs7PatchResult::PreconditionFailed;
        }
        if (r.failedAt == size_t(-1)) {
            return Rs7PatchResult::Ok;
        }

        if (!unrestored.empty()) {
            // Never retry over a site whose rollback failed. Keep it recorded as written, switch
            // off every feature that depends on it and on the failing site, stop.
            for (size_t j : unrestored) {
                const Rs7Group f = ops.group(j);
                const bool off = groups.disable(f);

                ops.log(std::string("UNRECOVERABLE ") + ops.name(j) +
                        ": restoring the original bytes or page protection failed - "
                        "the site remains unresolved, feature " +
                        groups.name(f) + (off ? " DISABLED" : " (core) cannot be disabled") +
                        ", no further patch rounds");
            }

            const Rs7Group ff = ops.group(todo[r.failedAt]);

            const bool ffOff = groups.disable(ff);

            ops.log(std::string("site ") + ops.name(todo[r.failedAt]) + " failed at writing -> feature " +
                    groups.name(ff) + (ffOff ? " DISABLED" : " (core) cannot be disabled") +
                    "; rollback incomplete (UNRECOVERABLE above)");
            written = unrestored;
            return Rs7PatchResult::Unrecoverable;
        }

        if (!Isolate(groups, ops, todo[r.failedAt], "writing")) {
            return Rs7PatchResult::CoreFailed;
        }
    }

    ops.log("patching did not converge - nothing patched");

    return Rs7PatchResult::CoreFailed;
}
