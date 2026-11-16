// Rs7Startup_LogSites (src/startup_sites.cpp) with its real Skipped / DeferredFg helpers, sliced out by
// test_startup_logsites.py; Writes (the feature switches) and Rs7Log are stubs.
#include "model/gates.h"
#include "model/groups.h"
#include "platform/hooktable.h"
#include "platform/resolve.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>


static std::vector<std::string> g_lines;
static std::set<std::string> g_off;  // ids whose feature switch is off (Writes false)
// Stubs: only GD1's group (Diag) is disabled.
Rs7Feature Rs7_FeatureOf(const Rs7Hook& h) {
    return std::strcmp(h.id, "GD1") ? Rs7Feature::Core : Rs7Feature::Diag;
}
bool Rs7_FeatureDisabled(Rs7Feature f) {
    return f == Rs7Feature::Diag;
}
const char* Rs7_FeatureName(Rs7Feature f) {
    return f == Rs7Feature::Diag ? "Diag" : "Core";
}

void Rs7Log(const char* fmt, ...) {
    char buf[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    g_lines.push_back(buf);
}

namespace {
bool Writes(const Rs7Hook& h) {
    return !g_off.count(h.id);
}
#include "logsites_helpers.inc"
}  // namespace

#include "logsites_body.inc"

static int g_fail = 0;

static void Expect(size_t i, const char* needle) {
    if (i >= g_lines.size() || !std::strstr(g_lines[i].c_str(), needle)) {
        std::printf("FAIL line %u: want \"%s\", got \"%s\"\n", unsigned(i), needle,
                    i < g_lines.size() ? g_lines[i].c_str() : "<none>");
        ++g_fail;
    }
}

int main() {
    struct Row {
        const char* id;
        bool ref, privateOnly;
        int pegs, strs, fg, pitch, ng;
        bool off;
        const char* want;
    };
    // Each row: one site, its hook's condition fields, whether its switch is off, and the expected log text.
    const Row rows[] = {
        {"RF1", true, false, 0, 0, 0, 0, 0, false, "resolved RF1     at 00001000 -> 00002000"},
        {"SK1", false, false, 0, 0, 0, 0, 0, false, "resolved SK1  at 00001000 ([debug] skip_sites: not patched)"},
        // Skipped and deferred: the skip wins (it is never written, not even later on the game thread).
        {"FGR1", false, false, 0, 0, 1, 0, 0, false, "([debug] skip_sites: not patched)"},
        // Skipped but its switch is off anyway: the switch is the reason, not the skip list.
        {"SK2", false, false, 0, 0, 0, 0, 0, true, "(diagnostics off: not patched)"},
        {"FGR2", false, false, 0, 0, 1, 0, 0, false, "deferred FGR2 at 00001000"},
        // FGHK / FGCT are native-fretgrid mid-hooks written with the main batch, never deferred.
        {"FGHK", false, false, 0, 0, 1, 0, 0, false, "patched FGHK at 00001000 (original target 00002000)"},
        {"PT1", false, false, 0, 0, 0, 0, 0, false, "patched PT1  at 00001000 (original target 00002000)"},
        {"PV1", false, true, 1, 0, 0, 0, 0, true, "(the private extras off: not patched)"},
        {"TV1", false, false, 1, 0, 0, 0, 0, true, "([test] force_* off: not patched)"},
        {"PG1", false, false, 1, 1, 0, 0, 0, true, "(the native PegSet off: not patched)"},
        {"ST1", false, false, 0, 1, 1, 0, 0, true, "(the native GuitarStrings off: not patched)"},
        {"FG1", false, false, 0, 0, 1, 1, 0, true, "(the native FretGrid off: not patched)"},
        {"PI1", false, false, 0, 0, 0, 1, 1, true, "(the native pitch storage off: not patched)"},
        {"NG1", false, false, 0, 0, 0, 0, 1, true, "(the native next-note grid off: not patched)"},
        {"DG1", false, false, 0, 0, 0, 0, 0, true, "(diagnostics off: not patched)"},
        {"GD1", false, false, 0, 0, 0, 0, 0, true, "(group Diag disabled: not patched)"},
    };
    const size_t n = sizeof rows / sizeof rows[0];
    std::vector<Rs7Site> sites(n);
    std::vector<Rs7Hook> hooks(n);
    std::vector<const Rs7Hook*> ptrs;
    Rs7ResolveResult res{};

    for (size_t i = 0; i < n; ++i) {
        sites[i] = Rs7Site{rows[i].id, "", 0, "", Rs7Kind::Jmp, 5, -1, 4, 5};
        hooks[i] = Rs7Hook{};
        hooks[i].id = rows[i].id;
        hooks[i].ref = rows[i].ref;
        hooks[i].privateOnly = rows[i].privateOnly;
        hooks[i].gates = (rows[i].pegs ? kRs7GatePegs : 0u) | (rows[i].strs ? kRs7GateStrings : 0u) |
                         (rows[i].fg ? kRs7GateFretgrid : 0u) | (rows[i].pitch ? kRs7GatePitch : 0u) |
                         (rows[i].ng ? kRs7GateNextgrid : 0u);
        if (rows[i].off) {
            g_off.insert(rows[i].id);
        }
        res.sites.push_back(Rs7Resolved{&sites[i], 0x1000, 0x2000, {}});
    }

    for (const Rs7Hook& h : hooks) {
        ptrs.push_back(&h);
    }

    Rs7Startup_LogSites(res, ptrs, ",SK1,FGR1,SK2,");

    for (size_t i = 0; i < n; ++i) {
        Expect(i, rows[i].want);
    }

    if (g_lines.size() != n) {
        std::printf("FAIL: %u lines for %u sites\n", unsigned(g_lines.size()), unsigned(n));
        ++g_fail;
    }

    // Skipped(): only whole ids between commas match; an empty or bare-comma list skips nothing.
    struct SkipCase {
        const char* list;
        const char* id;
        bool want;
    };
    const SkipCase skips[] = {
        {",PT1,", "PT1", true}, {",PT1,", "PT", false},   {",PT10,", "PT1", false}, {",XPT1,", "PT1", false},
        {"", "PT1", false},     {",,", "PT1", false},     {",A,PT1,", "PT1", true},  {",PT1,B,", "B", true},
    };

    for (const SkipCase& c : skips) {
        if (Skipped(c.list, c.id) != c.want) {
            std::printf("FAIL Skipped(\"%s\", \"%s\") != %d\n", c.list, c.id, int(c.want));
            ++g_fail;
        }
    }

    if (g_fail) {
        return 1;
    }

    std::puts("PASS: LogSites reasons (ref, skip, deferred, patched, every off switch) and Skipped id matching");
    return 0;
}
