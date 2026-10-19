#pragma once
#include <string>

// The [assets] settings and the self-build state of the 7-string headstock model. Written by Rs7Asset_Configure
// (startup, assetload_config.cpp); the nif handler (assetload_load.cpp) and the texture loader read them.
namespace rs7asset {
extern std::wstring g_dir;      // the model folder (the built-in default is relative to the DLL's folder)
extern std::wstring g_suffix;   // the edited-file rule: <dir>\<base><suffix>.nif
extern std::wstring g_file7;    // "" = the edited-file rule only
extern std::wstring g_file7Lh;  // the same for the left-handed in-line frame ([assets] headstock7_file_lh)
// [assets] selfbuild=1 with no usable headstock7_file: the model is built once from the game's own stock
// assets (hsbuild_io.cpp) into g_dir the first time a guitar frame loads, then used as g_file7. Each
// handedness has its own file, switch and state (the left-handed build needs the right-handed textures).
extern bool g_sbWanted, g_sbWantedLh;
extern std::wstring g_sbIni, g_sbDllDir;
extern volatile long g_sbState, g_sbStateLh;  // 0 untried, 1 running, 2 done
extern float g_wood;                          // [assets] headstock7_wood: brightness of the model's wood colour texture
}  // namespace rs7asset
