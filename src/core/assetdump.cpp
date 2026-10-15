#include "core/assetdump.h"
#include "core/config.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <intrin.h>
#include <memory>
#include <vector>

std::wstring Rs7AssetDump_SafeRelPath(const char* name) {
    if (!name || !*name) {
        return {};
    }

    std::wstring out;

    for (const char* p = name; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return {};
        }

        out.push_back(c == '/' ? L'\\' : wchar_t(c));
    }

    if (out[0] == L'\\') {
        return {};
    }

    // Reject any ".." path component.
    size_t start = 0;

    for (;;) {
        const size_t end = out.find(L'\\', start);
        const std::wstring part = out.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part.empty() || part == L"." || part == L"..") {
            return {};
        }
        if (end == std::wstring::npos) {
            break;
        }

        start = end + 1;
    }

    return out;
}

int Rs7AssetDump_Priority(const std::wstring& rel) {
    std::wstring s = rel;

    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    if (s.rfind(L"base\\dependency_guitar", 0) == 0) {
        return 0;
    }
    if (s == L"guitars.psarc") {
        return 1;
    }
    if (s.rfind(L"base\\", 0) == 0) {
        return 2;
    }
    if (s == L"cache.psarc" || s == L"static.psarc") {
        return 3;
    }
    if (s.rfind(L"venues\\", 0) == 0) {
        return 4;
    }
    if (s == L"crowd.psarc" || s == L"gears.psarc" || s == L"session.psarc") {
        return 5;
    }
    if (s.rfind(L"guitarcade\\", 0) == 0) {
        return 6;
    }
    if (s.rfind(L"dlc\\", 0) == 0) {
        return 9;
    }

    return 8;  // video, songs, audio, etudes: large, mostly not 3D assets
}

namespace {
// September 2022 exe only (Rs7AssetDump_Start checks the PE checksum). All CONFIRMED statically,
// docs/ARCHITECTURE.md "Dump everything".
constexpr uint32_t kChecksum = 0x00B13D7C;
constexpr uintptr_t kFsFrontend = 0x13657C4;  // *: VFS frontend, vtbl +0x10 = Open(path, &file)
constexpr uintptr_t kPsarcCreate = 0x8A33A0;  // ECX = stream, 6 cdecl args, returns archive

using Open_t = bool(__thiscall*)(void* self, const char* path, void** out);
using Ref_t = long(__thiscall*)(void* self);
using OpenEntry_t = bool(__thiscall*)(void* archive, const char* name, void** out);
using Read_t = int(__thiscall*)(void* file, void* buf, int size);
using GetSize_t = uint32_t(__thiscall*)(void* file);

void** Vt(void* o) {
    return *static_cast<void***>(o);
}

void AddRef(void* o) {
    reinterpret_cast<Ref_t>(Vt(o)[1])(o);
}

void Release(void* o) {
    reinterpret_cast<Ref_t>(Vt(o)[2])(o);
}

// Scope owners: a C++ exception in the dump loop (allocation, logging) still releases the game references and
// closes the output file (review #10).
struct RefGuard {
    void* p;

    ~RefGuard() {
        if (p) {
            Release(p);
        }
    }
};

struct HandleGuard {
    HANDLE h;

    ~HandleGuard() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

// Psarc_CreateFromStream(ECX = stream, id 0, flags 1 (keep names), 0, 0, 0, names NULL).
__declspec(naked) void* __stdcall CallPsarcCreate(void* /*stream*/) {
    __asm {
        push ebp
        mov ebp, esp
        push 0
        push 0
        push 0
        push 0
        push 1
        push 0
        mov ecx, [ebp + 8]
        mov eax, kPsarcCreate
        call eax
        add esp, 0x18
        pop ebp
        ret 4
    }
}

int SehFilter(const char* what, const char* name) {
    Rs7Log("assetdump: exception in %s (%s) - skipped", what, name ? name : "?");

    return EXCEPTION_EXECUTE_HANDLER;
}

bool SafeOpen(void* fe, const char* path, void** out) {
    __try {
        return reinterpret_cast<Open_t>(Vt(fe)[4])(fe, path, out);
    } __except (SehFilter("open", path)) {
        return false;
    }
}

void* SafeCreate(void* stream, const char* path) {
    __try {
        return CallPsarcCreate(stream);
    } __except (SehFilter("archive", path)) {
        return nullptr;
    }
}

bool SafeOpenEntry(void* archive, const char* name, void** out) {
    __try {
        return reinterpret_cast<OpenEntry_t>(Vt(archive)[4])(archive, name, out);
    } __except (SehFilter("entry", name)) {
        return false;
    }
}

int SafeRead(void* file, void* buf, int size, const char* name) {
    __try {
        return reinterpret_cast<Read_t>(Vt(file)[3])(file, buf, size);
    } __except (SehFilter("read", name)) {
        return -1;
    }
}

uint32_t SafeSize(void* file) {
    __try {
        return reinterpret_cast<GetSize_t>(Vt(file)[4])(file);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// SEH kept separate from vector-owning functions (MSVC C2712).
void* HeadstockFrontend() {
    __try {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return nullptr;
        }

        const auto nt =
            reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.CheckSum != kChecksum) {
            return nullptr;
        }

        return *reinterpret_cast<void**>(kFsFrontend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadHeadstockEntry(void* archive, const char* name, std::vector<uint8_t>& bytes) {
    void* file = nullptr;
    if (!SafeOpenEntry(archive, name, &file) || !file) {
        return false;
    }

    AddRef(file);
    const auto size = SafeSize(file);
    bool ok = size > 0 && size <= (64u << 20);

    try {
        if (ok) {
            bytes.resize(size);
            size_t got = 0;

            while (got < size) {
                const int n = SafeRead(file, bytes.data() + got, int(size - got), name);

                if (n <= 0 || uint32_t(n) > size - got) {
                    ok = false;
                    break;
                }

                got += uint32_t(n);
            }
        }
    } catch (...) {
        ok = false;
    }

    Release(file);
    if (!ok) {
        bytes.clear();
    }

    return ok;
}

bool MakeDirs(const std::wstring& path) {  // all directories of `path` (a file path)
    for (size_t i = 3; i < path.size(); ++i) {
        if (path[i] == L'\\') {
            CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
        }
    }

    return true;
}

struct Totals {
    unsigned files = 0, failed = 0;
    uint64_t bytes = 0;
};

// One archive: game path "nativeexe/<rel with />", output <outDir>\<rel>\<entry>.
void DumpArchive(const std::wstring& rel, const std::wstring& outDir, std::vector<char>& buf, Totals& all) {
    std::string path = "nativeexe/";

    for (wchar_t c : rel) {
        path.push_back(c == L'\\' ? '/' : char(towlower(c)));
    }

    const std::wstring base = outDir + L"\\" + rel;
    const std::wstring marker = base + L"\\.done";

    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Rs7Log("assetdump: %s already done - skipped", path.c_str());
        return;
    }

    void* fe = *reinterpret_cast<void**>(kFsFrontend);
    void* stream = nullptr;

    if (!fe || !SafeOpen(fe, path.c_str(), &stream) || !stream) {
        Rs7Log("assetdump: %s: the game's file system cannot open it - skipped", path.c_str());
        ++all.failed;
        return;
    }

    AddRef(stream);
    void* archive = SafeCreate(stream, path.c_str());

    Release(stream);  // the archive holds its own reference

    if (!archive) {
        Rs7Log("assetdump: %s: not readable as an archive by the game - skipped", path.c_str());
        ++all.failed;
        return;
    }

    AddRef(archive);
    RefGuard archiveRef{archive};
    auto* a = static_cast<uint8_t*>(archive);
    void** names = *reinterpret_cast<void***>(a + 0xA0);
    const size_t n = (*reinterpret_cast<uintptr_t*>(a + 0xA4) - reinterpret_cast<uintptr_t>(names)) / sizeof(void*);
    const uint8_t* toc = *reinterpret_cast<uint8_t**>(a + 0x58);

    Rs7Log("assetdump: %s: %u entries (TOC count %u incl. manifest)", path.c_str(), unsigned(n),
           toc ? unsigned(*reinterpret_cast<const uint32_t*>(toc + 0x14)) : 0u);
    unsigned files = 0, failed = 0;
    uint64_t bytes = 0;

    for (size_t i = 0; i < n; ++i) {
        const char* name = *static_cast<const char**>(names[i]);

        if (i < 3) {
            Rs7Log("assetdump:   e.g. \"%s\"", name ? name : "(null)");
        }
        const std::wstring relEntry = Rs7AssetDump_SafeRelPath(name);
        void* file = nullptr;

        if (relEntry.empty() || !SafeOpenEntry(archive, name, &file) || !file) {
            Rs7Log("assetdump:   %s: cannot open - skipped", name ? name : "(null)");
            ++failed;
            continue;
        }

        AddRef(file);
        RefGuard fileRef{file};
        const uint32_t size = SafeSize(file);
        const std::wstring out = base + L"\\" + relEntry;

        MakeDirs(out);
        const HANDLE h =
            CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HandleGuard outFile{h};
        uint64_t got = 0;
        bool ok = h != INVALID_HANDLE_VALUE;

        while (ok) {
            const int r = SafeRead(file, buf.data(), int(buf.size()), name);

            if (r < 0) {
                ok = false;
            }
            if (r <= 0) {
                break;
            }

            DWORD w = 0;

            ok = WriteFile(h, buf.data(), DWORD(r), &w, nullptr) && w == DWORD(r);
            got += uint32_t(r);
        }

        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            outFile.h = INVALID_HANDLE_VALUE;
        }

        if (!ok || got != size) {
            Rs7Log("assetdump:   %s: %s (%llu of %u bytes)", name, ok ? "size mismatch" : "FAILED",
                   static_cast<unsigned long long>(got), size);
            ++failed;
        }

        ++files;
        bytes += got;
    }

    Rs7Log("assetdump: %s: %u files, %llu bytes, %u failed", path.c_str(), files,
           static_cast<unsigned long long>(bytes), failed);
    all.files += files;
    all.failed += failed;
    all.bytes += bytes;

    if (!failed) {
        const HANDLE m =
            CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (m != INVALID_HANDLE_VALUE) {
            CloseHandle(m);
        }
    }
}

void FindArchives(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FindArchives(root, rel + name + L"\\", out);
        } else if (name.size() > 6) {
            std::wstring ext = name.substr(name.size() - 6);

            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".psarc") {
                out.push_back(rel + name);
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

struct Job {
    std::wstring gameDir, outDir;
    bool now;
};

volatile long g_state = 0;  // Rs7AssetDumpStatus state: 0 idle, 1 waiting for the game, 2 running, 3 done, 4 failed
volatile long g_archivesDone = 0, g_archives = 0, g_files = 0;

// The dump proper. The Job is owned by the caller (DumpCleanup), never copied.
void DumpWork(void* p) {
    const Job& job = *static_cast<Job*>(p);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    // The game's file system: d3d9 loads after the unpack; then wait for the VFS frontend and
    // give the game time to reach its menus.
    for (int i = 0; i < 240 && !GetModuleHandleW(L"d3d9.dll"); ++i) {
        Sleep(250);
    }

    for (int i = 0; i < 240 && !*reinterpret_cast<void* volatile*>(kFsFrontend); ++i) {
        Sleep(250);
    }

    if (!job.now) {
        Sleep(30000);  // at startup: let the game reach its menus (the overlay starts it there)
    }

    if (!*reinterpret_cast<void* volatile*>(kFsFrontend)) {
        Rs7Log("assetdump: the game's file system did not come up - no dump");
        _InterlockedExchange(&g_state, 0);
        return;
    }

    _InterlockedExchange(&g_state, 2);
    std::vector<std::wstring> archives;

    FindArchives(job.gameDir, L"", archives);
    std::stable_sort(archives.begin(), archives.end(), [](const std::wstring& a, const std::wstring& b) {
        return Rs7AssetDump_Priority(a) < Rs7AssetDump_Priority(b);
    });
    Rs7Log("assetdump: %u archives -> %ls", unsigned(archives.size()), job.outDir.c_str());
    std::vector<char> buf(1 << 20);
    Totals all;
    const DWORD t0 = GetTickCount();

    _InterlockedExchange(&g_archives, long(archives.size()));

    for (const auto& rel : archives) {
        DumpArchive(rel, job.outDir, buf, all);
        _InterlockedIncrement(&g_archivesDone);
        _InterlockedExchange(&g_files, long(all.files));
    }

    _InterlockedExchange(&g_state, 3);
    Rs7Log("assetdump: DONE - %u files, %llu MB, %u failed, %lu s", all.files,
           static_cast<unsigned long long>(all.bytes >> 20), all.failed, (GetTickCount() - t0) / 1000);
}

void DumpCleanup(void* p) {
    delete static_cast<Job*>(p);
}

DWORD WINAPI DumpThread(LPVOID p) {
    Rs7AssetDump_RunWorker(DumpWork, p, DumpCleanup);

    return 0;
}

// SEH only here (no C++ objects needing unwinding: C2712): a hardware fault in the game's reader.
bool SafeWork(void (*work)(void*), void* context) {
    __try {
        work(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

// The worker's exception boundary: nothing may escape the thread entry. A failure leaves the observable state 4,
// logs it, and cleanup runs exactly once either way.
bool Rs7AssetDump_RunWorker(void (*work)(void*), void* context, void (*cleanup)(void*)) {
    bool ok = false;

    try {
        ok = SafeWork(work, context);
    } catch (...) {
        ok = false;
    }

    if (!ok) {
        _InterlockedExchange(&g_state, 4);

        try {
            Rs7Log("assetdump: the dump worker FAILED (exception) - dump stopped");
        } catch (...) {
        }
    }

    if (cleanup) {
        try {
            cleanup(context);
        } catch (...) {
        }
    }

    return ok;
}

namespace {
bool LaunchImpl(const wchar_t* iniPath, const wchar_t* gameDir, bool now) {
    if (_InterlockedCompareExchange(&g_state, 1, 0) != 0) {
        return false;  // one dump per run
    }

    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);

    if (nt->OptionalHeader.CheckSum != kChecksum) {
        Rs7Log("assetdump: unsupported game build - no dump");
        _InterlockedExchange(&g_state, 0);
        return false;
    }

    wchar_t dir[MAX_PATH] = {};

    GetPrivateProfileStringW(L"debug", L"dump_dir", L"", dir, MAX_PATH, iniPath);
    std::wstring out = dir;

    if (out.empty()) {
        wchar_t profile[MAX_PATH] = {};

        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        out = std::wstring(profile) + L"\\Desktop\\rocksmith-assets";
    }

    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) {
        out.pop_back();
    }

    CreateDirectoryW(out.c_str(), nullptr);
    std::wstring game = gameDir;

    while (!game.empty() && (game.back() == L'\\' || game.back() == L'/')) {
        game.pop_back();
    }

    Rs7Log("assetdump: %s -> every .psarc of %ls via the game's own reader, to %ls",
           now ? "started from the overlay" : "[debug] dump_assets=1", game.c_str(), out.c_str());
    std::unique_ptr<Job> job(new Job{game, out, now});
    const HANDLE t = CreateThread(nullptr, 0, DumpThread, job.get(), 0, nullptr);

    if (!t) {
        _InterlockedExchange(&g_state, 0);
        return false;
    }

    job.release();  // the worker owns it now
    CloseHandle(t);

    return true;
}

bool Launch(const wchar_t* iniPath, const wchar_t* gameDir, bool now) {
    try {
        return LaunchImpl(iniPath, gameDir, now);
    } catch (...) {  // allocation failure while building the job: report, never throw into the caller
        _InterlockedExchange(&g_state, 4);
        return false;
    }
}
}  // namespace

void Rs7AssetDump_Start(const wchar_t* iniPath, const wchar_t* gameDir) {
    if (!Rs7Config_ReadInt(L"debug", L"dump_assets", 0, iniPath)) {
        return;
    }

    Launch(iniPath, gameDir, false);
}

bool Rs7AssetDump_Trigger(const wchar_t* iniPath, const wchar_t* gameDir) {
    return Launch(iniPath, gameDir, true);
}

Rs7AssetDumpStatus Rs7AssetDump_Status() {
    Rs7AssetDumpStatus st;

    st.state = int(g_state);
    st.archivesDone = int(g_archivesDone);
    st.archives = int(g_archives);
    st.files = int(g_files);

    return st;
}

namespace {
bool ReadTemplatePair(const char* psarc, const char* guitarEntry, std::vector<uint8_t>& guitar,
                      std::vector<uint8_t>& peg) {
    guitar.clear();
    peg.clear();
    void* fe = HeadstockFrontend();
    if (!fe) {
        return false;
    }

    void* stream = nullptr;
    if (!SafeOpen(fe, psarc, &stream) || !stream) {
        return false;
    }

    AddRef(stream);
    void* archive = SafeCreate(stream, psarc);

    Release(stream);
    if (!archive) {
        return false;
    }

    AddRef(archive);
    const bool ok = ReadHeadstockEntry(archive, guitarEntry, guitar) &&
                    ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/epi_whilshire_peg.nif", peg);

    Release(archive);

    if (!ok) {
        guitar.clear();
        peg.clear();
    }

    return ok;
}
}  // namespace

bool Rs7AssetDump_ReadHeadstockTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg) {
    return ReadTemplatePair("nativeexe/base/dependency_guitarstandardinlinedrh.psarc",
                            "assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif", guitar, peg);
}

// Gitarrenkopfplatte 3+3 (assetload.cpp): the in-line peg and the one texture of it that the 3+3 peg package
// does not carry (wilshire_hardware.dds; hardware_gloss / loft_env_map / normal_sm have 3+3 namesakes).
bool Rs7AssetDump_ReadInlinePeg(std::vector<uint8_t>& peg, std::vector<uint8_t>& hardware) {
    peg.clear();
    hardware.clear();
    const char* psarc = "nativeexe/base/dependency_guitarstandardinlinedrh.psarc";
    void* fe = HeadstockFrontend();
    if (!fe) {
        return false;
    }

    void* stream = nullptr;
    if (!SafeOpen(fe, psarc, &stream) || !stream) {
        return false;
    }

    AddRef(stream);
    void* archive = SafeCreate(stream, psarc);

    Release(stream);
    if (!archive) {
        return false;
    }

    AddRef(archive);
    const bool ok =
        ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/epi_whilshire_peg.nif", peg) &&
        ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/wilshire_hardware.dds", hardware);

    Release(archive);

    if (!ok) {
        peg.clear();
        hardware.clear();
    }

    return ok;
}

// The left-handed in-line frame; its archive carries the same peg (identical bytes, CONFIRMED by SHA-256 on the
// owner's install), so the left-handed package is self-contained.
bool Rs7AssetDump_ReadHeadstockTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg) {
    return ReadTemplatePair("nativeexe/base/dependency_guitarstandardinlinedlh.psarc",
                            "assets/gameplay/guitar_standard_inline/epi_whilshire_cherry_lh.nif", guitar, peg);
}
