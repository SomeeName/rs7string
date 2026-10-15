#include "hooks/isolate.h"
#include "core/resolve.h"

namespace {
// Disables the group of a failing site; false if it is Core or a member of the native 7-string unit (no fallback).
bool Isolate(const Rs7PatchOps& ops, size_t i, const char* stage) {
    const Rs7Feature f = ops.feature(i);

    if (Rs7_IsSevenUnit(f)) {
        ops.log(std::string("site ") + ops.name(i) + " failed at " + stage +
                " and belongs to the native 7-string unit (" + Rs7_FeatureName(f) +
                ", no legacy fallback) - nothing patched, the game stays stock");
        return false;
    }

    if (f == Rs7Feature::Core || !Rs7_DisableFeature(f)) {
        ops.log(std::string("site ") + ops.name(i) + " failed at " + stage +
                " and belongs to the core - nothing patched");
        return false;
    }

    ops.log(std::string("site ") + ops.name(i) + " failed at " + stage + " -> feature " + Rs7_FeatureName(f) +
            " DISABLED, everything else is patched");

    return true;
}
}  // namespace

bool Rs7_IsolateResolveFailures(Rs7ResolveResult& res, const std::function<void(const std::string&)>& log) {
    for (size_t k = 0; k < res.failed.size(); ++k) {
        const Rs7Hook* h = Rs7_FindHook(res.failed[k]->id);
        const Rs7Feature f = h ? Rs7_FeatureOf(*h) : Rs7Feature::Core;
        const std::string why = k < res.errors.size() ? res.errors[k] : std::string(res.failed[k]->id);

        if (Rs7_IsSevenUnit(f)) {
            log("site error (native 7-string unit " + std::string(Rs7_FeatureName(f)) +
                ", no legacy fallback): " + why);
            res.sites = res.resolved;
            return false;
        }

        if (f == Rs7Feature::Core || !Rs7_DisableFeature(f)) {
            log("site error (core): " + why);
            res.sites = res.resolved;
            return false;
        }

        log("site error: " + why + " -> feature " + Rs7_FeatureName(f) + " DISABLED, everything else is patched");
    }

    res.sites = res.resolved;
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
    std::vector<size_t>* unrestored;  // capacity reserved: restore failures (review #8)
};

void WriteRound(void* p) {
    Round& r = *static_cast<Round*>(p);

    r.failedAt = size_t(-1);

    for (size_t k = 0; k < r.todo->size(); ++k) {
        const size_t i = (*r.todo)[k];

        if (r.ops->apply(i)) {
            r.written->push_back(i);
            continue;
        }

        r.failedAt = k;

        for (size_t j : *r.written) {
            if (!r.ops->restore(j)) {
                r.unrestored->push_back(j);
            }
        }

        r.written->clear();
        return;
    }
}
}  // namespace

Rs7PatchResult Rs7_PatchIsolated(const Rs7PatchOps& ops, std::vector<size_t>& written) {
    for (int round = 0; round <= int(Rs7Feature::Count); ++round) {
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
                if (!Isolate(ops, i, "preparation")) {
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
            std::vector<Rs7Range> ranges;

            ranges.reserve(todo.size());

            for (size_t i : todo) {
                ranges.push_back(ops.range(i));
            }

            Rs7FreezeStats st;
            const bool ran = Rs7_RunFrozen(ranges.data(), ranges.size(), &WriteRound, &r, &st, ops.freezeDeadlineMs);
            char line[256];

            Rs7_FreezeDescribe(st, line, sizeof line);
            std::string msg =
                std::string(line) + " (round " + std::to_string(round) + ", " + std::to_string(todo.size()) + " sites";

            if (st.timedOut && st.busyRange < todo.size()) {
                msg += std::string(", busy site ") + ops.name(todo[st.busyRange]);
            }
            ops.log(msg + ")");
            if (!ran) {
                return Rs7PatchResult::FreezeTimeout;
            }
        } else {
            WriteRound(&r);
        }

        if (r.failedAt == size_t(-1)) {
            return Rs7PatchResult::Ok;
        }

        if (!unrestored.empty()) {
            // Review #8: never retry over a site whose rollback failed. Keep it recorded as written, switch
            // off every feature that depends on it and on the failing site, stop.
            for (size_t j : unrestored) {
                const Rs7Feature f = ops.feature(j);
                const bool off = Rs7_DisableFeature(f);

                ops.log(std::string("UNRECOVERABLE ") + ops.name(j) +
                        ": restoring the original bytes failed (read-back differs) - "
                        "the site stays written, feature " +
                        Rs7_FeatureName(f) + (off ? " DISABLED" : " (core) cannot be disabled") +
                        ", no further patch rounds");
            }

            const Rs7Feature ff = ops.feature(todo[r.failedAt]);

            Rs7_DisableFeature(ff);
            ops.log(std::string("site ") + ops.name(todo[r.failedAt]) + " failed at writing -> feature " +
                    Rs7_FeatureName(ff) + " DISABLED; rollback incomplete (UNRECOVERABLE above)");
            written = unrestored;
            return Rs7PatchResult::Unrecoverable;
        }

        if (!Isolate(ops, todo[r.failedAt], "writing")) {
            return Rs7PatchResult::CoreFailed;
        }
    }

    ops.log("patching did not converge - nothing patched");

    return Rs7PatchResult::CoreFailed;
}
