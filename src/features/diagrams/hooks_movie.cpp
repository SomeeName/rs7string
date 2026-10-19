// Chord-diagram movie glue: loads the recipes from chord-diagrams\ and hooks Scaleform's FileOpener::OpenFile, which
// swaps the three owner movies for an in-memory IFile holding the patched bytes (movie.cpp).
#include "features/allocstats/allocstats.h"
#include "features/diagrams/diagrams.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/protect.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
constexpr size_t kLimit = rs7diagram::kMovieLimit;
std::array<std::vector<uint8_t>, 3> g_recipes;
bool g_trace = false;  // [debug] diagrams_trace=1
volatile LONG g_traceCalls = 0;
constexpr uint16_t kSprites[] = {1103, 28, 26};
const wchar_t* const kFiles[] = {L"commonlib.rs7da", L"contentpanel.rs7da", L"ge_game.rs7da"};
DWORD WINAPI InstallThread(void*);
}  // namespace

void Rs7DiagramMovie_Configure(const wchar_t* ini) {
    if (!g_rs7_diagrams) {
        return;
    }

    g_trace = Rs7Config_ReadInt(L"debug", L"diagrams_trace", 0, ini) != 0;
    std::wstring path(ini);

    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"chord-diagrams\\";

    for (int i = 0; i < 3; ++i) {
        std::ifstream f(std::filesystem::path(path + kFiles[i]), std::ios::binary | std::ios::ate);
        auto size = f ? f.tellg() : std::streampos(-1);

        if (size < 84 || size > std::streampos(kLimit)) {
            Rs7Log("diagrams: missing/invalid recipe %ls", kFiles[i]);
            continue;
        }

        g_recipes[i].resize(size_t(size));
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(g_recipes[i].data()), size)) {
            g_recipes[i].clear();
        }
        if (g_trace) {
            Rs7Log("diagrams: trace recipe %ls loaded=%d (%d bytes)", kFiles[i], int(!g_recipes[i].empty()), int(size));
        }
    }

    bool any = false;

    for (const auto& r : g_recipes) {
        any = any || !r.empty();
    }

    if (!any) {
        Rs7Log("diagrams: no recipe loaded; FileOpener hook not installed");
        return;
    }

    if (HANDLE t = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr)) {
        CloseHandle(t);
    } else {
        Rs7Log("diagrams: cannot start FileOpener hook thread");
    }
}

namespace {
// VFS IFile ABI established by features/headstock/assetload_tex.cpp. Heap ownership allows Scaleform
// to retain the file past the synchronous handler; the stock adapter remains stock.
struct File {
    const void* const* vt = nullptr;
    volatile LONG refs = 1;
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;

    File() { Rs7Alloc_Inc(kAkDiagFiles); }

    ~File() { Rs7Alloc_Dec(kAkDiagFiles); }

    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

void __fastcall Destroy(File* f, void*, uint32_t) {
    delete f;
}

LONG __fastcall Add(File* f, void*) {
    return InterlockedIncrement(&f->refs);
}

LONG __fastcall Release(File* f, void*) {
    LONG n = InterlockedDecrement(&f->refs);

    if (!n) {
        delete f;
    }

    return n;
}

uint32_t __fastcall Read(File* f, void*, void* p, uint32_t n) {
    n = std::min(n, uint32_t(f->bytes.size()) - f->pos);

    if (n) {
        if (!p) {
            return 0;
        }

        std::memcpy(p, f->bytes.data() + f->pos, n);
        f->pos += n;
    }

    return n;
}

uint32_t __fastcall Size(File* f, void*) {
    return uint32_t(f->bytes.size());
}

uint32_t __fastcall Tell(File* f, void*) {
    return f->pos;
}

bool __fastcall Seek(File* f, void*, uint32_t pos) {
    if (pos > f->bytes.size()) {
        return false;
    }

    f->pos = pos;

    return true;
}

uint64_t __fastcall Size64(File* f, void*) {
    return f->bytes.size();
}

bool __fastcall True(File*, void*) {
    return true;
}

const void* const kVt[] = {
    reinterpret_cast<void*>(&Destroy), reinterpret_cast<void*>(&Add),    reinterpret_cast<void*>(&Release),
    reinterpret_cast<void*>(&Read),    reinterpret_cast<void*>(&Size),   reinterpret_cast<void*>(&Tell),
    reinterpret_cast<void*>(&Seek),    reinterpret_cast<void*>(&Size64), reinterpret_cast<void*>(&True),
    reinterpret_cast<void*>(&True),    reinterpret_cast<void*>(&True),   reinterpret_cast<void*>(&True)};
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

// Reads all n bytes of the game's IFile into *bytes and seeks back to `position`. May throw (allocation).
bool ReadWholeFile(void* file, uint32_t n, uint32_t position, std::vector<uint8_t>* bytes, const char** why) {
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

    const bool restored = seek(file, position);

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

namespace rs7diagram {
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

    try {
        if (n < 13 || n > kLimit) {
            *why = "size out of range";
            return false;
        }

        std::vector<uint8_t> bytes, patched;
        if (!ReadWholeFile(file, n, position, &bytes, why)) {
            return false;
        }

        if (!SwapMovie(bytes.data(), n, g_recipes[target].data(), g_recipes[target].size(), kSprites[target],
                       &patched)) {
            *why = "unsupported movie/hash (SwapMovie refused)";
            return false;
        }

        replacement = std::make_unique<File>();
        replacement->vt = kVt;
        replacement->bytes = std::move(patched);
    } catch (...) {
        *why = "allocation/recipe failure";
        return false;
    }

    File* owned = replacement.release();

    Rs7Log("diagrams: verified one DoInitAction swapped for %s (%u -> %u)", url, n, unsigned(owned->bytes.size()));
    *reinterpret_cast<File**>(adapter + 0x18) = owned;
    *reinterpret_cast<uint32_t*>(adapter + 4) = 0;
    reinterpret_cast<FRelease>(vt[2])(file);

    return true;
}
}  // namespace rs7diagram

namespace {
void* __fastcall OpenHook(void* self, void*, const char* url, void* heap, int flags, int mode) {
    void* w = g_open(self, url, heap, flags, mode);

    if (w && g_rs7_diagrams && rs7diagram::MovieTarget(url) >= 0) {
        const char* why = "";

        if (!rs7diagram::SwapWrapperFile(w, url, &why)) {
            Rs7Log("diagrams: refused %s (%s)", url ? url : "(null)", why);
        }
    }

    return w;
}

DWORD WINAPI InstallThread(void*) {
    // The slot may not hold the stock opener yet (unpack); poll, never write blindly.
    for (int i = 0; i < 60000 / 10; ++i) {
        if (rs7diagram::InstallOpenHook(g_slot, kOpenFile)) {
            return 0;
        }

        Sleep(10);
    }

    Rs7Log("diagrams: FileOpener slot never reached 0x%X; movie swap disabled", unsigned(kOpenFile));

    return 0;
}
}  // namespace

namespace rs7diagram {
// Replaces *slot with the hook once it equals expect. False means "not yet" or failure.
bool InstallOpenHook(uintptr_t* slot, uintptr_t expect) {
    if (g_open) {
        return true;
    }
    if (IsBadReadPtr(slot, 4) || *slot != expect) {
        return false;
    }

    DWORD old;

    if (!Rs7_Protect(slot, 4, PAGE_READWRITE, &old)) {
        Rs7Log("diagrams: cannot unprotect FileOpener slot");
        return false;
    }

    g_open = reinterpret_cast<OpenFn>(*slot);
    *slot = reinterpret_cast<uintptr_t>(&OpenHook);
    DWORD tmp;

    Rs7_Protect(slot, 4, old, &tmp);
    Rs7Log("diagrams: FileOpener::OpenFile hooked (slot %p, original 0x%X)", static_cast<void*>(slot),
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
}  // namespace rs7diagram
