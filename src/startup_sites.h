#pragma once
#include "platform/isolate.h"
#include <cstddef>
#include <string>
#include <vector>

// The main batch of Rs7_Startup (src/startup.cpp): writing the resolved sites in isolated rounds, the FretGrid
// sites deferred to the game thread, and the per-site log. `hooks[i]` is the hook of `res.sites[i]`; `skip` is
// [debug] skip_sites as ",ID,...,".
struct Rs7Hook;
struct Rs7ResolveResult;

// Prepares and writes every site the features' switches enable (platform/isolate.h); `todo` gets the written ones.
Rs7PatchResult Rs7Startup_WriteSites(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks,
                                     const std::string& skip, std::vector<size_t>& todo);
// After the main batch: hands the FretGrid layout sites the final round enables to the first guitar update
// (rs7fg::SetDeferredApply). Returns their count.
size_t Rs7Startup_DeferFretGrid(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks,
                                const std::string& skip);
// One log line per site: resolved, deferred, patched or why not.
void Rs7Startup_LogSites(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks);
