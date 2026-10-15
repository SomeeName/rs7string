#include "core/diagrammovie.h"
#include "core/allocstats.h"
#include "core/config.h"
#include "core/diagrams.h"
#include "core/protect.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#pragma comment(lib, "bcrypt.lib")

namespace {
constexpr size_t kLimit = 16 * 1024 * 1024;

uint32_t U32(const uint8_t* p) {
    uint32_t v;

    std::memcpy(&v, p, 4);

    return v;
}

uint16_t U16(const uint8_t* p) {
    uint16_t v;

    std::memcpy(&v, p, 2);

    return v;
}

bool Hash(const uint8_t* p, size_t n, const uint8_t* expected) {
    uint8_t h[32];

    return n <= kLimit &&
           BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(p), ULONG(n), h, 32) >= 0 &&
           std::memcmp(h, expected, 32) == 0;
}

bool Span(const uint8_t* p, size_t n, uint16_t sprite, size_t* a, size_t* b) {
    if (!p || n < 13 || n > kLimit || (std::memcmp(p, "GFX", 3) && std::memcmp(p, "FWS", 3)) || U32(p + 4) != n) {
        return false;
    }

    int bits = p[8] >> 3;
    size_t pos = 8 + (5 + 4 * bits + 7) / 8 + 4;
    if (!bits || pos > n) {
        return false;
    }

    int found = 0;

    while (pos < n) {
        size_t start = pos;
        if (n - pos < 2) {
            return false;
        }

        uint16_t word = U16(p + pos);

        pos += 2;
        size_t len = word & 63;

        if (len == 63) {
            if (n - pos < 4) {
                return false;
            }

            len = U32(p + pos);
            pos += 4;
        }

        if (len > n - pos) {
            return false;
        }

        if ((word >> 6) == 59 && len >= 2 && U16(p + pos) == sprite) {
            *a = start;
            *b = pos + len;
            ++found;
        }

        if ((word >> 6) == 0) {
            return len == 0 && pos == n && found == 1;
        }

        pos += len;
    }

    return false;
}

std::array<std::vector<uint8_t>, 3> g_recipes;
bool g_trace = false;  // [debug] diagrams_trace=1
volatile LONG g_traceCalls = 0;
constexpr uint16_t kSprites[] = {1103, 28, 26};
const wchar_t* const kFiles[] = {L"commonlib.rs7da", L"contentpanel.rs7da", L"ge_game.rs7da"};
}  // namespace

namespace rs7diagram {
int MovieTarget(const char* name) {
    if (!name) {
        return -1;
    }

    std::string s(name);

    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return c == '\\' ? '/' : char(std::tolower(c)); });
    if (s.compare(0, 5, "data/") == 0) {
        s.erase(0, 5);
    }
    const char* const names[] = {"gfxassets/libs/commonlib.gfx", "gfxassets/views/contentpanel.gfx",
                                 "gfxassets/views/ge_game.gfx"};

    for (int i = 0; i < 3; ++i) {
        if (s == names[i]) {
            return i;
        }
    }

    return -1;
}

bool SwapMovie(const uint8_t* p, size_t n, const uint8_t* r, size_t rn, uint16_t sprite, std::vector<uint8_t>* out) {
    size_t a = 0, b = 0;
    if (!out || !r || rn < 84 || rn > kLimit || std::memcmp(r, "R7DA\x01\0\0\0", 8) || !Span(p, n, sprite, &a, &b)) {
        return false;
    }

    size_t oldSize = U32(r + 8), newSize = U32(r + 12), count = U32(r + 16);
    if (oldSize != b - a || newSize > kLimit || count > rn - 84 || !Hash(p + a, b - a, r + 20)) {
        return false;
    }

    std::vector<uint8_t> tag;

    tag.reserve(newSize);
    size_t pos = 84;

    for (size_t i = 0; i < count; ++i) {
        if (pos >= rn) {
            return false;
        }

        uint8_t op = r[pos++];

        if (op == 0) {
            if (rn - pos < 8) {
                return false;
            }

            size_t at = U32(r + pos), len = U32(r + pos + 4);

            pos += 8;
            if (!len || at > oldSize || len > oldSize - at || len > newSize - tag.size()) {
                return false;
            }

            tag.insert(tag.end(), p + a + at, p + a + at + len);
        } else if (op == 1) {
            if (rn - pos < 4) {
                return false;
            }

            size_t len = U32(r + pos);

            pos += 4;
            if (!len || len > rn - pos || len > newSize - tag.size()) {
                return false;
            }

            tag.insert(tag.end(), r + pos, r + pos + len);
            pos += len;
        } else {
            return false;
        }
    }

    if (pos != rn || tag.size() != newSize || !Hash(tag.data(), tag.size(), r + 52) || n - oldSize + newSize > kLimit) {
        return false;
    }

    std::vector<uint8_t> result(p, p + a);

    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), p + b, p + n);
    uint32_t size = uint32_t(result.size());

    std::memcpy(result.data() + 4, &size, 4);
    size_t x = 0, y = 0;
    if (!Span(result.data(), result.size(), sprite, &x, &y) || x != a || y != a + tag.size()) {
        return false;
    }

    *out = std::move(result);

    return true;
}
}  // namespace rs7diagram

namespace {
DWORD WINAPI InstallThread(void*);
}

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
// VFS IFile ABI established by core/assetload.cpp. Heap ownership allows Scaleform
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
    auto seek = reinterpret_cast<FSeek>(vt[6]);
    auto read = reinterpret_cast<FRead>(vt[3]);
    const uint32_t position = tell(file), n = size(file);
    std::unique_ptr<File> replacement;

    try {
        if (n < 13 || n > kLimit) {
            *why = "size out of range";
            return false;
        }

        if (!seek(file, 0)) {
            *why = "seek to 0 failed";
            return false;
        }

        std::vector<uint8_t> bytes(n), patched;
        uint32_t done = 0;

        while (done < n) {
            uint32_t got = read(file, bytes.data() + done, n - done);
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
