#pragma once
#include "features/headstock/hsbuild.h"
#include <filesystem>
#include <string>
// In-DLL headstock builder (issue #28). RsxrAsset_SelfBuild is called once, lazily, by the nif handler
// (assetload_load.cpp, first guitar or bass frame) when [assets] selfbuild=1 and no usable headstock7_file exists:
// the package's output files go into destDir (the [assets] dir); outputs that differ from the package's manifest
// hashes (an update shipped a new .bin) are rebuilt. Returns true for cached/built; failures are logged and return
// false (stock model stays).
enum class RsxrHsResult { Cached, Built, Failed };
using RsxrHsTemplateReader = bool (*)(rsxrhs::Bytes& guitar, rsxrhs::Bytes& peg);
// Testable core: package + destination + template source. replaceDiffering=false leaves any existing file alone
// (only missing ones are written; all present = Cached); true = hash-verify and rewrite stale ones. Explicit model
// paths in protectedFiles are excluded from checking and writing, including dependency builds.
RsxrHsResult RsxrHs_Ensure(const rsxrhs::Bytes& package, const std::filesystem::path& dest, RsxrHsTemplateReader reader,
                           bool replaceDiffering, std::string& note,
                           const std::vector<std::wstring>& protectedFiles = {});
// Every unprotected output in a valid manifest must be a non-empty regular file.
bool RsxrHs_OutputsPresent(const rsxrhs::Bytes& package, const std::filesystem::path& dest,
                           const std::vector<std::wstring>& protectedFiles = {});
// One package next to the DLL and the game files it is built from:
// - Guitar   rsxr_headstock.bin         in-line guitar frame + peg: the guitar models, their pegs, all shared
//                                             textures (env / normal map) and the guitar texture sets;
// - GuitarLh rsxr_headstock_lh.bin      left-handed in-line frame + peg: the mirrored guitar models;
// - Bass     rsxr_headstock_bass.bin    in-line bass frame + peg: the bass models, pegs and texture sets;
// - BassLh   rsxr_headstock_bass_lh.bin left-handed bass frame + peg: the mirrored bass models.
// Every kit but Guitar references the shared textures, so the caller builds Guitar first.
enum class RsxrHsKit { Guitar, GuitarLh, Bass, BassLh };
// The caller has already checked [assets] selfbuild (assetload_config.cpp).
bool RsxrAsset_SelfBuild(const wchar_t* dllDirectory, const wchar_t* destDir, RsxrHsKit kit = RsxrHsKit::Guitar,
                         const std::vector<std::wstring>& protectedFiles = {});
// Whether the kit's package next to the DLL differs (size or write time) from the one destDir's outputs were last built
// or verified from (destDir\<package>.stamp, written by a successful self-build; no stamp = changed), or any required
// output is absent/empty. False without a package. An update or incomplete cache makes the next start rebuild.
bool RsxrAsset_PackageChanged(const wchar_t* dllDirectory, const wchar_t* destDir, RsxrHsKit kit,
                              const std::vector<std::wstring>& protectedFiles = {});
