// Rs7_FreezeDescribe: the log line of one Rs7_RunFrozen call (freeze.h).
#include "platform/freeze.h"
#include <cstdio>

void Rs7_FreezeDescribe(const Rs7FreezeStats& st, char* out, size_t cap) {
    int w = 0;

    if (st.readerBusy) {
        w = std::snprintf(
            out, cap, "freeze: REFUSED - the thread reader is still blocked from an earlier freeze; nothing written");
    } else if (st.wedged) {
        w = std::snprintf(
            out, cap,
            "freeze: WEDGED after %.1f ms - the thread reader did not return within %u ms after every thread was "
            "resumed; nothing written, later freezes refused until it returns",
            st.totalMs, st.graceMs);
    } else if (st.tooMany) {
        w = std::snprintf(out, cap, "freeze: more than %u threads - nothing written",
                          unsigned(Rs7FreezeStats::kMaxThreads));
    } else if (st.timedOut && st.lastWasStall) {
        w = std::snprintf(
            out, cap,
            "freeze: DEADLINE after %.1f ms, %u stalls, %u released and %u retries - thread control kept stalling; "
            "nothing written",
            st.totalMs, st.stalls, st.released, st.retries);
    } else if (st.timedOut) {
        w = std::snprintf(out, cap,
                          "freeze: DEADLINE after %.1f ms and %u retries - thread %lu stayed at %08X inside range %u; "
                          "nothing written",
                          st.totalMs, st.retries, st.busyTid, unsigned(st.busyEip), unsigned(st.busyRange));
    } else {
        w = std::snprintf(
            out, cap,
            "freeze: %u threads suspended for %.3f ms, %u retries, %u stalls, %u released (%.1f ms total, "
            "%u skipped)",
            st.threads, st.frozenMs, st.retries, st.stalls, st.released, st.totalMs, st.skipped);
    }

    for (unsigned i = 0; i < st.nEvents && w >= 0 && size_t(w) < cap; ++i) {
        const auto& e = st.events[i];
        const char* op = e.op == 'S'   ? "SuspendThread"
                         : e.op == 'C' ? "GetThreadContext"
                         : e.op == 'L' ? "thread listing"
                                       : "job pickup";
        int k;

        if (e.releaseMs >= 0) {
            k = std::snprintf(
                out + w, cap - size_t(w),
                "; stall %u: %s(thread %lu) no progress for %.0f ms -> all resumed, reader back after %.1f ms", i + 1,
                op, e.tid, double(e.waitMs), double(e.releaseMs));
        } else {
            k = std::snprintf(out + w, cap - size_t(w),
                              "; stall %u: %s(thread %lu) no progress for %.0f ms -> all resumed, reader NOT back",
                              i + 1, op, e.tid, double(e.waitMs));
        }

        if (k < 0) {
            break;
        }

        w += k;
    }

    if (st.stalls > st.nEvents && w >= 0 && size_t(w) < cap) {
        std::snprintf(out + w, cap - size_t(w), "; +%u more stalls", st.stalls - st.nEvents);
    }
}
