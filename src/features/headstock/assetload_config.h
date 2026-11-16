#pragma once
#include "features/headstock/assetload.h"
#include <string>
#include <vector>

// Asset paths retain their construction order here; scalar settings and build state live in rs7head::State (state.h).
namespace rs7asset {
struct ConfigState {
    // Startup writes before hooks are installed; loader threads then read without a lock.
    std::wstring dir;                      // model folder; the default is relative to the DLL's folder
    std::wstring suffix = kDefaultSuffix;  // edited-file rule: <dir>\<base><suffix>.nif
    // Startup initializes; a loader build owner writes, published by rs7head::State::sbState / sbStateLh (interlocked).
    std::wstring file7;    // in-line model (7 or 8 pegs); "" = the edited-file rule only
    std::wstring file7Lh;  // left-handed in-line model
    // Startup writes; loader threads then read without a lock. Second model (rs7_alt5) for seven strings.
    std::wstring file7s, file7sLh;
    // Startup writes; loader builds then read without a lock.
    std::wstring sbDllDir;
    std::vector<std::wstring> sbProtected;  // explicit paths: dependency builds must never write them
};

extern ConfigState g_config;
}  // namespace rs7asset
