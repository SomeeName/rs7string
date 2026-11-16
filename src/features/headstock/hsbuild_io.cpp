#include "features/headstock/hsbuild_io.h"
#include "features/headstock/assetload_tex.h"
#include "features/headstock/headstockread.h"
#include "features/headstock/hsbuild.h"
#include "features/headstock/state.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>

using rs7head::g_head;

namespace {
bool Read(const std::filesystem::path& path, rs7hs::Bytes& bytes) {
    return rs7asset::ReadFileBytes(path.wstring(), bytes);
}

bool Protected(const std::filesystem::path& path, const std::vector<std::wstring>& files) {
    if (files.empty()) {
        return false;
    }

    // MSVC resolves junctions via GetFinalPathNameByHandleW, including the existing parent of a missing file.
    const auto normalized = std::filesystem::weakly_canonical(std::filesystem::absolute(path));

    for (const auto& f : files) {
        const auto other = std::filesystem::weakly_canonical(std::filesystem::absolute(f));

        if (_wcsicmp(normalized.c_str(), other.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

// A failed read is not proof that a path is absent (directories, sharing/ACL errors). A 0-byte file counts as
// absent: no output is ever empty (an interrupted copy or a truncated file), so it is rebuilt, not kept forever.
bool Missing(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};

    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !data.nFileSizeHigh && !data.nFileSizeLow;
    }

    const DWORD error = GetLastError();

    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

HANDLE CreateTemp(const std::filesystem::path& path, const std::vector<std::wstring>& protectedFiles,
                  std::filesystem::path& temp) {
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        temp = path.wstring() + L".building." + std::to_wstring(GetCurrentProcessId()) + L"." +
               std::to_wstring(InterlockedIncrement(&g_head.buildSequence));
        if (Protected(temp, protectedFiles)) {
            continue;
        }

        const auto h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return INVALID_HANDLE_VALUE;
        }
    }

    return INVALID_HANDLE_VALUE;
}

// Every write destination, including stamps and temporary files, must preserve configured custom models.
bool Write(const std::filesystem::path& path, const rs7hs::Bytes& bytes,
           const std::vector<std::wstring>& protectedFiles) {
    if (Protected(path, protectedFiles)) {
        return true;
    }

    std::filesystem::path temp;
    const auto h = CreateTemp(path, protectedFiles, temp);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(h, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() &&
                    FlushFileBuffers(h);

    CloseHandle(h);
    if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }

    DeleteFileW(temp.c_str());

    return false;
}
}  // namespace

bool Rs7Hs_OutputsPresent(const rs7hs::Bytes& package, const std::filesystem::path& dest,
                          const std::vector<std::wstring>& protectedFiles) {
    std::vector<rs7hs::Expected> expected;
    std::string error;

    if (!rs7hs::Manifest(package, expected, error)) {
        return false;
    }

    try {
        for (const auto& f : expected) {
            const auto path = dest / f.name;

            if (!Protected(path, protectedFiles) && !rs7asset::FilePresent(path.wstring())) {
                return false;
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }

    return true;
}

Rs7HsResult Rs7Hs_Ensure(const rs7hs::Bytes& package, const std::filesystem::path& dest, Rs7HsTemplateReader reader,
                         bool replaceDiffering, std::string& note, const std::vector<std::wstring>& protectedFiles) {
    try {
        std::string error;
        std::vector<rs7hs::Expected> expected;

        if (!rs7hs::Manifest(package, expected, error)) {
            note = "invalid package: " + error;
            return Rs7HsResult::Failed;
        }

        rs7hs::Bytes existing;
        bool allThere = true, allMatch = true;

        for (const auto& f : expected) {
            if (Protected(dest / f.name, protectedFiles)) {
                continue;
            }

            if (!Read(dest / f.name, existing)) {
                if (!replaceDiffering && !Missing(dest / f.name)) {
                    note = "existing output unreadable: " + f.name + "; no output touched";
                    return Rs7HsResult::Failed;
                }

                allThere = false;
                allMatch = false;
                continue;
            }

            if (replaceDiffering && (existing.size() != f.size || rs7hs::Hash(existing) != f.sha256)) {
                allMatch = false;
            }
        }

        if (replaceDiffering ? allMatch : allThere) {
            note = "all " + std::to_string(expected.size()) + " files present";
            return Rs7HsResult::Cached;
        }

        rs7hs::Bytes guitar, peg;

        if (!reader || !reader(guitar, peg)) {
            note = "game reader unavailable/unsupported or template read failed";
            return Rs7HsResult::Failed;
        }

        if (!rs7hs::CheckTemplates(guitar, peg, package, error)) {
            note = error + "; no output touched";
            return Rs7HsResult::Failed;
        }

        std::vector<rs7hs::File> outputs;

        if (!rs7hs::Build(guitar, peg, package, outputs, error)) {
            note = "refused: " + error + "; no output touched";
            return Rs7HsResult::Failed;
        }

        std::filesystem::create_directories(dest);
        unsigned written = 0;

        for (const auto& f : outputs) {
            const auto path = dest / f.name;
            if (Protected(path, protectedFiles)) {
                continue;
            }

            const bool there = Read(path, existing);
            if (there && (existing == f.bytes || !replaceDiffering)) {
                continue;
            }

            if (!Write(path, f.bytes, protectedFiles)) {
                note = "write failed: " + path.u8string() + "; partial build can be retried";
                return Rs7HsResult::Failed;
            }

            ++written;
        }

        note = "verified build, wrote " + std::to_string(written) + " files";
        return Rs7HsResult::Built;
    } catch (const std::exception& e) {
        note = std::string("failure: ") + e.what();
        return Rs7HsResult::Failed;
    } catch (...) {
        note = "failure";
        return Rs7HsResult::Failed;
    }
}

namespace {
struct Kit {
    const wchar_t* bin;
    Rs7HsTemplateReader reader;
    const char* label;  // log prefix
};

constexpr Kit kKits[] = {
    {L"rs7string_headstock.bin", Rs7HeadstockRead_HeadstockTemplates, ""},
    {L"rs7string_headstock_lh.bin", Rs7HeadstockRead_HeadstockTemplatesLh, "left-handed "},
    {L"rs7string_headstock_bass.bin", Rs7HeadstockRead_BassTemplates, "bass "},
    {L"rs7string_headstock_bass_lh.bin", Rs7HeadstockRead_BassTemplatesLh, "left-handed bass "},
};

// "<size> <write time>" of a package file, "" if it cannot be read: an update that ships a new package changes it.
std::string PackageStamp(const std::filesystem::path& bin) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(bin.c_str(), GetFileExInfoStandard, &a)) {
        return {};
    }

    char text[64];

    std::snprintf(text, sizeof text, "%lu:%lu %lu:%lu", a.nFileSizeHigh, a.nFileSizeLow,
                  a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);

    return text;
}

// The stamp of the package the outputs in destDir were last built or verified from: destDir\<package>.stamp.
std::filesystem::path StampPath(const wchar_t* destDir, const Kit& k) {
    return std::filesystem::path(destDir) / (std::wstring(k.bin) + L".stamp");
}
}  // namespace

bool Rs7Asset_PackageChanged(const wchar_t* dllDirectory, const wchar_t* destDir, Rs7HsKit kit,
                             const std::vector<std::wstring>& protectedFiles) {
    if (!dllDirectory || !*dllDirectory || !destDir || !*destDir) {
        return false;
    }

    const Kit& k = kKits[static_cast<int>(kit)];
    const std::string now = PackageStamp(std::filesystem::path(dllDirectory) / k.bin);
    if (now.empty()) {
        return false;  // no package: nothing to rebuild from
    }

    rs7hs::Bytes last, package;

    return !Read(StampPath(destDir, k), last) || std::string(last.begin(), last.end()) != now ||
           !Read(std::filesystem::path(dllDirectory) / k.bin, package) ||
           !Rs7Hs_OutputsPresent(package, destDir, protectedFiles);
}

bool Rs7Asset_SelfBuild(const wchar_t* dllDirectory, const wchar_t* destDir, Rs7HsKit kit,
                        const std::vector<std::wstring>& protectedFiles) {
    if (!dllDirectory || !*dllDirectory || !destDir || !*destDir) {
        return false;
    }

    const std::filesystem::path root(dllDirectory);
    if (!root.is_absolute()) {
        return false;
    }

    rs7hs::Bytes package;
    const Kit& k = kKits[static_cast<int>(kit)];
    const wchar_t* const bin = k.bin;

    if (!Read(root / bin, package)) {
        Rs7Log("hsbuild: missing/unreadable %ls next to DLL", bin);
        return false;
    }

    std::string note;
    const auto r = Rs7Hs_Ensure(package, std::filesystem::path(destDir), k.reader, true, note, protectedFiles);

    Rs7Log("hsbuild: %s%s in %ls: %s", k.label,
           r == Rs7HsResult::Failed   ? "FAILED"
           : r == Rs7HsResult::Cached ? "cached"
                                      : "built",
           destDir, note.c_str());

    if (r == Rs7HsResult::Failed) {
        return false;
    }

    // The outputs match this package now: remember it, so the next start only rebuilds after an update.
    const std::string stamp = PackageStamp(root / bin);

    if (!stamp.empty() && !Write(StampPath(destDir, k), rs7hs::Bytes(stamp.begin(), stamp.end()), protectedFiles)) {
        Rs7Log("hsbuild: %scould not write %ls.stamp (the next start checks the models again)", k.label, bin);
    }

    return true;
}
