#include "features/headstock/hsbuild_io.h"
#include "features/headstock/headstockread.h"
#include "features/headstock/hsbuild.h"
#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <filesystem>

namespace {
bool Read(const std::filesystem::path& path, rs7hs::Bytes& bytes) {
    bytes.clear();
    const auto h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart <= (64ll << 20);

    try {
        if (ok) {
            bytes.resize(size_t(size.QuadPart));
            DWORD got = 0;

            ok = ReadFile(h, bytes.data(), DWORD(bytes.size()), &got, nullptr) && got == bytes.size();
        }
    } catch (...) {
        ok = false;
    }

    CloseHandle(h);
    if (!ok) {
        bytes.clear();
    }

    return ok;
}

// A failed read is not proof that a path is absent (empty files, directories, sharing/ACL errors).
bool Missing(const std::filesystem::path& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    const DWORD error = GetLastError();

    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool Write(const std::filesystem::path& path, const rs7hs::Bytes& bytes, bool replace) {
    const auto temp = std::filesystem::path(path.wstring() + L".building");
    const auto h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(h, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() &&
                    FlushFileBuffers(h);

    CloseHandle(h);
    if (ok &&
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        return true;
    }

    DeleteFileW(temp.c_str());

    return false;
}
}  // namespace

Rs7HsResult Rs7Hs_Ensure(const rs7hs::Bytes& package, const std::filesystem::path& dest, Rs7HsTemplateReader reader,
                         bool replaceDiffering, std::string& note) {
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
            const bool there = Read(path, existing);
            if (there && (existing == f.bytes || !replaceDiffering)) {
                continue;
            }

            if (!Write(path, f.bytes, replaceDiffering)) {
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

bool Rs7Asset_SelfBuild(const wchar_t* iniPath, const wchar_t* dllDirectory, const wchar_t* destDir, bool force,
                        bool leftHanded) {
    if (!iniPath) {
        return false;
    }
    if (!force && !Rs7Config_ReadInt(L"assets", L"selfbuild", 1, iniPath)) {
        return true;
    }
    if (!dllDirectory || !*dllDirectory || !destDir || !*destDir) {
        return false;
    }

    const std::filesystem::path root(dllDirectory);
    if (!root.is_absolute()) {
        return false;
    }

    rs7hs::Bytes package;
    const wchar_t* const bin = leftHanded ? L"rs7string_headstock_lh.bin" : L"rs7string_headstock.bin";

    if (!Read(root / bin, package)) {
        Rs7Log("hsbuild: missing/unreadable %ls next to DLL", bin);
        return false;
    }

    std::string note;
    const auto r = Rs7Hs_Ensure(
        package, std::filesystem::path(destDir),
        leftHanded ? Rs7HeadstockRead_HeadstockTemplatesLh : Rs7HeadstockRead_HeadstockTemplates, false, note);

    Rs7Log("hsbuild: %s%s in %ls: %s", leftHanded ? "left-handed " : "",
           r == Rs7HsResult::Failed   ? "FAILED"
           : r == Rs7HsResult::Cached ? "cached"
                                      : "built",
           destDir, note.c_str());

    return r != Rs7HsResult::Failed;
}
