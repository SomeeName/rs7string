// Chord-diagram movie glue: loads the recipes from chord-diagrams\ and hooks Scaleform's FileOpener::OpenFile, which
// swaps the three owner movies for an in-memory IFile holding the patched bytes (movie.cpp).
#include "features/allocstats/allocstats.h"
#include "features/diagrams/diagrams.h"
#include "model/memoryfile.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/protect.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
constexpr size_t kLimit = rsxrdiagram::kMovieLimit;
std::array<std::vector<uint8_t>, 3> g_recipes;
bool g_trace = false;  // [debug] diagrams_trace=1
volatile LONG g_traceCalls = 0;
constexpr uint16_t kSprites[] = {1103, 28, 26};
const wchar_t* const kFiles[] = {L"commonlib.rsxrda", L"contentpanel.rsxrda", L"ge_game.rsxrda"};
DWORD WINAPI InstallThread(void*);
}  // namespace

void RsxrDiagramMovie_Configure(const wchar_t* ini) {
    if (!g_rsxr_diagrams) {
        return;
    }

    g_trace = RsxrConfig_ReadInt(L"debug", L"diagrams_trace", 0, ini) != 0;
    std::wstring path(ini);

    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"chord-diagrams\\";

    for (int i = 0; i < 3; ++i) {
        std::ifstream f(std::filesystem::path(path + kFiles[i]), std::ios::binary | std::ios::ate);
        auto size = f ? f.tellg() : std::streampos(-1);

        if (size < 84 || size > std::streampos(kLimit)) {
            RsxrLog("diagrams: missing/invalid recipe %ls", kFiles[i]);
            continue;
        }

        g_recipes[i].resize(size_t(size));
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(g_recipes[i].data()), size)) {
            g_recipes[i].clear();
        }
        if (g_trace) {
            RsxrLog("diagrams: trace recipe %ls loaded=%d (%d bytes)", kFiles[i], int(!g_recipes[i].empty()),
                    int(size));
        }
    }

    bool any = false;

    for (const auto& r : g_recipes) {
        any = any || !r.empty();
    }

    if (!any) {
        RsxrLog("diagrams: no recipe loaded; FileOpener hook not installed");
        return;
    }

    if (HANDLE t = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr)) {
        CloseHandle(t);
    } else {
        RsxrLog("diagrams: cannot start FileOpener hook thread");
    }
}

namespace {
// VFS IFile ABI established by features/headstock/assetload_tex.cpp. Heap ownership allows Scaleform
// to retain the file past the synchronous handler; the stock adapter remains stock.
struct File : rsxr::MemoryFile {
    std::vector<uint8_t> bytes;

    File() { RsxrAlloc_Inc(kAkDiagFiles); }

    ~File() { RsxrAlloc_Dec(kAkDiagFiles); }

    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

// Scalar deleting destructor (MSVC slot 0): bit 0 of flags asks for the free. File always comes from new
// (std::make_unique in SwapWrapperFile), so delete matches the allocation; without the bit only destroy.
void __fastcall Destroy(rsxr::MemoryFile* base, void*, uint32_t flags) {
    auto* f = static_cast<File*>(base);

    if (flags & 1) {
        delete f;
    } else {
        f->~File();
    }
}

long __fastcall Release(rsxr::MemoryFile* base, void*) {
    auto* f = static_cast<File*>(base);
    const long n = rsxr::MemoryFileOps::DropRef(f);

    if (!n) {
        delete f;
    }

    return n;
}

// Owned bytes: null reads fail without advancing; all status slots report true as before.
const auto kVt = rsxr::MemoryFileVtable<false, false>(Destroy, Release);
using Get = uint32_t(__thiscall*)(void*);
using FSeek = bool(__thiscall*)(void*, uint32_t);
using FRead = uint32_t(__thiscall*)(void*, void*, uint32_t);
using FRelease = LONG(__thiscall*)(void*);
// Scaleform FileOpener::OpenFile (vtable 0x121F2C4 slot 3, data word 0x121F2D0 = 0xC30330):
// thiscall, 4 stack args, callee pops 16.
using OpenFn = void*(__thiscall*)(void*, const char*, void*, int, int);
OpenFn g_open = nullptr;
uintptr_t* g_slot = reinterpret_cast<uintptr_t*>(0x121F2D0);
constexpr uintptr_t kOpenFile = 0xC30330;

// Reads all n bytes of the game's IFile into *bytes and seeks back to 0 (the caller only swaps unread
// streams). May throw (allocation).
bool ReadWholeFile(void* file, uint32_t n, std::vector<uint8_t>* bytes, const char** why) {
    auto vt = *reinterpret_cast<void***>(file);
    auto seek = reinterpret_cast<FSeek>(vt[6]);
    auto read = reinterpret_cast<FRead>(vt[3]);

    if (!seek(file, 0)) {
        *why = "seek to 0 failed";
        return false;
    }

    bytes->resize(n);
    uint32_t done = 0;

    while (done < n) {
        uint32_t got = read(file, bytes->data() + done, n - done);
        if (!got || got > n - done) {
            break;
        }

        done += got;
    }

    const bool restored = seek(file, 0);

    if (!restored) {
        *why = "stream position restore failed";
        return false;
    }

    if (done != n) {
        *why = "short read";
        return false;
    }

    return true;
}
}  // namespace

namespace rsxrdiagram {
// Wrapper layout (CONFIRMED, docs/ARCHITECTURE.md): w+8 = NiFile adapter,
// adapter+0x18 = owned IFile, adapter+4 = cached position, adapter+0x10 = valid byte.
// On any failure the wrapper is left untouched and *why says what happened.
bool SwapWrapperFile(void* w, const char* url, const char** why) {
    const char* dummy;

    if (!why) {
        why = &dummy;
    }
    const int target = MovieTarget(url);

    if (target < 0) {
        *why = "not a target movie";
        return false;
    }

    if (g_recipes[target].empty()) {
        *why = "no recipe loaded";
        return false;
    }

    if (!w) {
        *why = "null wrapper";
        return false;
    }

    uint8_t* adapter = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(w) + 8);

    if (!adapter) {
        *why = "no NiFile adapter at wrapper+8";
        return false;
    }

    void* file = *reinterpret_cast<void**>(adapter + 0x18);

    if (!file) {
        *why = "no IFile at adapter+0x18";
        return false;
    }

    auto vt = *reinterpret_cast<void***>(file);
    auto tell = reinterpret_cast<Get>(vt[5]), size = reinterpret_cast<Get>(vt[4]);
    const uint32_t position = tell(file), n = size(file);
    std::unique_ptr<File> replacement;

    // The patched movie has a different layout, so a non-zero position has no equivalent in it; the
    // replacement starts at 0 (adapter+4 is reset below), which is only correct for an unread stream.
    if (position != 0) {
        *why = "stream already read (position != 0)";
        return false;
    }

    try {
        if (n < 13 || n > kLimit) {
            *why = "size out of range";
            return false;
        }

        std::vector<uint8_t> bytes, patched;
        if (!ReadWholeFile(file, n, &bytes, why)) {
            return false;
        }

        if (!SwapMovie(bytes.data(), n, g_recipes[target].data(), g_recipes[target].size(), kSprites[target],
                       &patched)) {
            *why = "unsupported movie/hash (SwapMovie refused)";
            return false;
        }

        replacement = std::make_unique<File>();
        replacement->vt = kVt.data();
        replacement->bytes = std::move(patched);
        replacement->data = replacement->bytes.data();
        replacement->size = uint32_t(replacement->bytes.size());
    } catch (...) {
        *why = "allocation/recipe failure";
        return false;
    }

    File* owned = replacement.release();

    RsxrLog("diagrams: verified one DoInitAction swapped for %s (%u -> %u)", url, n, unsigned(owned->bytes.size()));
    *reinterpret_cast<File**>(adapter + 0x18) = owned;
    *reinterpret_cast<uint32_t*>(adapter + 4) = 0;
    reinterpret_cast<FRelease>(vt[2])(file);

    return true;
}
}  // namespace rsxrdiagram

namespace {
void* __fastcall OpenHook(void* self, void*, const char* url, void* heap, int flags, int mode) {
    void* w = g_open(self, url, heap, flags, mode);

    // SwapWrapperFile checks the target again; this one only keeps non-target opens out of the log.
    if (w && g_rsxr_diagrams && rsxrdiagram::MovieTarget(url) >= 0) {
        const char* why = "";

        if (!rsxrdiagram::SwapWrapperFile(w, url, &why)) {
            RsxrLog("diagrams: refused %s (%s)", url, why);
        }
    }

    return w;
}

DWORD WINAPI InstallThread(void*) {
    // The slot may not hold the stock opener yet (unpack); poll, never write blindly.
    for (int i = 0; i < 60000 / 10; ++i) {
        if (rsxrdiagram::InstallOpenHook(g_slot, kOpenFile)) {
            return 0;
        }

        Sleep(10);
    }

    RsxrLog("diagrams: FileOpener slot never reached 0x%X; movie swap disabled", unsigned(kOpenFile));

    return 0;
}
}  // namespace

namespace rsxrdiagram {
// Replaces *slot with the hook once it equals expect. False means "not yet" or failure.
bool InstallOpenHook(uintptr_t* slot, uintptr_t expect) {
    if (g_open) {
        return true;
    }

    uintptr_t cur = 0;

    if (!Rsxr_SafeCopy(&cur, slot, sizeof cur) || cur != expect) {
        return false;
    }

    DWORD old;

    if (!Rsxr_Protect(slot, 4, PAGE_READWRITE, &old)) {
        RsxrLog("diagrams: cannot unprotect FileOpener slot");
        return false;
    }

    g_open = reinterpret_cast<OpenFn>(*slot);
    *slot = reinterpret_cast<uintptr_t>(&OpenHook);
    DWORD tmp;

    Rsxr_Protect(slot, 4, old, &tmp);
    RsxrLog("diagrams: FileOpener::OpenFile hooked (slot %p, original 0x%X)", static_cast<void*>(slot),
            unsigned(expect));

    return true;
}

void* OpenHookForTest() {
    return reinterpret_cast<void*>(&OpenHook);
}

void SetOpenForTest(void* fn) {
    g_open = reinterpret_cast<OpenFn>(fn);
}

void SetRecipeForTest(int i, const std::vector<uint8_t>& r) {
    g_recipes[i] = r;
}
}  // namespace rsxrdiagram
