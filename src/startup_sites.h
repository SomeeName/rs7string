#pragma once
#include "platform/isolate.h"
#include <cstddef>
#include <string>
#include <vector>

// The main batch of Rsxr_Startup (src/startup.cpp): writing the resolved sites in isolated rounds, the FretGrid
// sites deferred to the game thread, and the per-site log. `hooks[i]` is the hook of `res.sites[i]`; `skip` is
// [debug] skip_sites as ",ID,...,".
struct RsxrHook;
struct RsxrResolveResult;

// Prepares and writes every site the features' switches enable (platform/isolate.h); `written` gets the written ones.
RsxrPatchResult RsxrStartup_WriteSites(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                                       const std::string& skip, std::vector<size_t>& written);
// After the main batch: hands the FretGrid layout sites the final round enables to the first guitar update
// (rsxrfg::SetDeferredApply). Returns their count.
size_t RsxrStartup_DeferFretGrid(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                                 const std::string& skip);
// One log line per site: resolved, deferred, patched or why not.
void RsxrStartup_LogSites(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                          const std::string& skip);
