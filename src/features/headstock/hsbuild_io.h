#pragma once
#include "features/headstock/hsbuild.h"
#include <filesystem>
#include <string>
// In-DLL headstock builder (issue #28). Rs7Asset_SelfBuild is called once, lazily, by the nif handler
// (assetload_load.cpp, first guitar frame) when [assets] selfbuild=1 and no usable headstock7_file exists:
// the nine output files go into destDir (the [assets] dir); existing files are never replaced.
// Returns true for disabled/cached/built; failures are logged and return false (stock model stays).
enum class Rs7HsResult { Cached, Built, Failed };
using Rs7HsTemplateReader = bool (*)(rs7hs::Bytes& guitar, rs7hs::Bytes& peg);
// Testable core: package + destination + template source. replaceDiffering=false leaves any existing file alone
// (only missing ones are written; all present = Cached); true = hash-verify and rewrite stale ones.
Rs7HsResult Rs7Hs_Ensure(const rs7hs::Bytes& package, const std::filesystem::path& dest, Rs7HsTemplateReader reader,
                         bool replaceDiffering, std::string& note);
// force=true ignores the ini switch (the caller already decided). leftHanded=true builds rs7_inline7_lh.nif from
// rs7string_headstock_lh.bin and the left-handed in-line frame (epi_whilshire_cherry_lh.nif); the textures it
// references come from the right-handed build, so the caller builds that one first.
bool Rs7Asset_SelfBuild(const wchar_t* iniPath, const wchar_t* dllDirectory, const wchar_t* destDir, bool force = false,
                        bool leftHanded = false);
