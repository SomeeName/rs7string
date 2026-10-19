#include "features/headstock/assetload_tex.h"
#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_config.h"
#include "features/headstock/assetload_scene.h"
#include "model/groups.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>
#include <utility>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_dds = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_nfctor = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_nfdtor = 0;
}

using namespace rs7asset;

std::vector<std::string> Rs7Asset_DdsNames(const uint8_t* buf, size_t size) {
    std::vector<std::string> out;
    if (!buf) {
        return out;
    }

    auto ok = [](uint8_t c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == '/' || c == '\\';
    };

    for (size_t i = 0; i + 4 <= size; ++i) {
        if (buf[i] != '.' || (buf[i + 1] | 0x20) != 'd' || (buf[i + 2] | 0x20) != 'd' || (buf[i + 3] | 0x20) != 's') {
            continue;
        }
        if (i + 4 < size && ok(buf[i + 4]) && buf[i + 4] != '/' && buf[i + 4] != '\\') {
            continue;  // ".ddsx", ".dds.bak"
        }

        size_t b = i;

        while (b > 0 && ok(buf[b - 1]) && i - b < 200) {
            --b;
        }

        size_t base = b;

        for (size_t k = b; k < i; ++k) {
            if (buf[k] == '/' || buf[k] == '\\') {
                base = k + 1;
            }
        }

        if (base == i) {
            continue;
        }

        std::string n(reinterpret_cast<const char*>(buf + base), i + 4 - base);

        if (!INameIn(n, out)) {
            out.push_back(n);
        }
    }

    return out;
}

bool Rs7Asset_ScaleDxt1(uint8_t* dds, size_t size, float k) {
    if (!dds || size < 128 + 8 || !(k > 0.0f && k < 1.0f)) {
        return false;
    }
    if (std::memcmp(dds, "DDS ", 4) != 0 || std::memcmp(dds + 84, "DXT1", 4) != 0) {
        return false;
    }

    auto scale = [k](uint16_t c) {
        const int r = int((c >> 11) * k + 0.5f), g = int(((c >> 5) & 63) * k + 0.5f), b = int((c & 31) * k + 0.5f);

        return uint16_t((r << 11) | (g << 5) | b);
    };

    for (size_t o = 128; o + 8 <= size; o += 8) {
        uint8_t* blk = dds + o;
        const uint16_t c0 = uint16_t(blk[0] | (blk[1] << 8)), c1 = uint16_t(blk[2] | (blk[3] << 8));
        uint16_t n0 = scale(c0), n1 = scale(c1);
        uint32_t idx = uint32_t(blk[4]) | (uint32_t(blk[5]) << 8) | (uint32_t(blk[6]) << 16) | (uint32_t(blk[7]) << 24);
        const bool four = c0 > c1;

        if (four && n0 == n1) {
            idx = 0;  // one colour left: every texel = c0 (index 3 would be black in 3-colour mode)
        } else if ((n0 > n1) !=
                   four) {  // order flipped: swap the end points, remap 0 <-> 1 (and 2 <-> 3 in 4-colour mode)
            std::swap(n0, n1);
            uint32_t out = 0;

            for (int t = 0; t < 16; ++t) {
                uint32_t v = (idx >> (2 * t)) & 3;

                v = v < 2 ? v ^ 1 : (four ? v ^ 1 : v);
                out |= v << (2 * t);
            }

            idx = out;
        }

        blk[0] = uint8_t(n0);
        blk[1] = uint8_t(n0 >> 8);
        blk[2] = uint8_t(n1);
        blk[3] = uint8_t(n1 >> 8);

        for (int i = 0; i < 4; ++i) {
            blk[4 + i] = uint8_t(idx >> (8 * i));
        }
    }

    return true;
}

bool Rs7Asset_IsWoodColour(const std::string& name) {
    std::string low = name;

    for (char& ch : low) {
        ch = char((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    }

    return low.find("wood") != std::string::npos && low.find("gloss") == std::string::npos &&
           low.find("_n.") == std::string::npos && low.find("normal") == std::string::npos;
}

namespace {
volatile long g_logTex = 0;

// ---- textures of the 7-string model (loader thread) ----
// The nidds stream handler 0x4A81D0 (cdecl name, NiFile*, ctx): reads the DDS through the game's DDS
// reader (0x9E8610 / 0x9E9720), then under the NiTexture list lock finds or creates the
// NiSourceTexture registered under `name` in the renderer's texture palette (+100h -> +90h, vtbl+8
// find / +0Ch add) and gives it the pixel data. Its stream has to be a NiFile: the reader calls
// NiFile virtuals past the end of NiMemStream's 7-slot vtable (a NiMemStream faulted in game at
// "stem" of the "NiSystem" string that follows that vtable). So, as Res_ParseFileJob_Load
// 0x4A7BB9..0x4A7C02 does: the game's NiFile adapter (Res_NiFileAdapter_Ctor 0x49FF90, ESI = 0x1C
// adapter, EDI = IFile*; destroyed by 0x4A0030), over an IFile - here our own in-memory one with the
// interface the resource manager's memory IFile (vtable 0x11A33F8) has.
struct MemFile {
    const void* const* vtable;
    volatile long refs;
    const uint8_t* data;
    uint32_t size;
    uint32_t pos;
};

// IFile methods, thiscall (MSVC: __fastcall with an unused EDX, the callee pops the stack args).
void __fastcall MfDtor(MemFile*, void*, uint32_t) {}

long __fastcall MfAddRef(MemFile* f, void*) {
    return _InterlockedIncrement(&f->refs);
}

long __fastcall MfRelease(MemFile* f, void*) {
    return _InterlockedDecrement(&f->refs);
}  // ours: never freed by the game

uint32_t __fastcall MfRead(MemFile* f, void*, void* dst, uint32_t n) {  // +0Ch
    const uint32_t left = f->size - f->pos;

    if (n > left) {
        n = left;
    }
    if (n && dst) {
        std::memcpy(dst, f->data + f->pos, n);
    }
    f->pos += n;

    return n;
}

uint32_t __fastcall MfSize(MemFile* f, void*) {
    return f->size;
}  // +10h

uint32_t __fastcall MfTell(MemFile* f, void*) {
    return f->pos;
}  // +14h

bool __fastcall MfSeek(MemFile* f, void*, uint32_t to) {  // +18h, absolute
    if (to > f->size) {
        return false;
    }

    f->pos = to;

    return true;
}

uint64_t __fastcall MfSize64(MemFile* f, void*) {
    return f->size;
}  // +1Ch (blob +18h u64 size)

bool __fastcall MfTrue(MemFile*, void*) {
    return true;
}  // +20h (0xC94340: mov al,1)

bool __fastcall MfGood(MemFile* f, void*) {
    return f->data != nullptr;
}  // +24h

const void* const kMemFileVt[] = {
    reinterpret_cast<const void*>(&MfDtor),    reinterpret_cast<const void*>(&MfAddRef),
    reinterpret_cast<const void*>(&MfRelease), reinterpret_cast<const void*>(&MfRead),
    reinterpret_cast<const void*>(&MfSize),    reinterpret_cast<const void*>(&MfTell),
    reinterpret_cast<const void*>(&MfSeek),    reinterpret_cast<const void*>(&MfSize64),
    reinterpret_cast<const void*>(&MfTrue),    reinterpret_cast<const void*>(&MfGood),
    reinterpret_cast<const void*>(&MfTrue),    reinterpret_cast<const void*>(&MfTrue),
};

DWORD g_ddsFaultCode = 0;
uintptr_t g_ddsFaultAt = 0;

int DdsFilter(EXCEPTION_POINTERS* ep) {
    g_ddsFaultCode = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    g_ddsFaultAt = ep && ep->ExceptionRecord ? uintptr_t(ep->ExceptionRecord->ExceptionAddress) : 0;

    return EXCEPTION_EXECUTE_HANDLER;
}

__declspec(noinline) void AdapterCtor(uint8_t* adapter, MemFile* file) {  // 0x49FF90: ESI = adapter, EDI = IFile
    __asm {
        push ebx
        push esi
        push edi
        mov esi, adapter
        mov edi, file
        call dword ptr [g_rs7_fn_nfctor]
        pop edi
        pop esi
        pop ebx
    }
}

__declspec(noinline) void AdapterDtor(uint8_t* adapter) {  // 0x4A0030: ECX = adapter
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, adapter
        call dword ptr [g_rs7_fn_nfdtor]
        pop edi
        pop esi
        pop ebx
    }
}

std::wstring DirOf(const std::wstring& path) {
    const size_t k = path.find_last_of(L"\\/");

    return k == std::wstring::npos ? std::wstring() : path.substr(0, k + 1);
}
}  // namespace

namespace rs7asset {
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64ll << 20);

    try {
        if (ok) {
            out.resize(size_t(sz.QuadPart));
            DWORD got = 0;

            ok = ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size();
        }
    } catch (...) {
        ok = false;
    }

    CloseHandle(h);
    if (!ok) {
        out.clear();
    }

    return ok;
}

void* CallDdsHandler(const char* key, const uint8_t* bytes, uint32_t size) {
    g_ddsFaultCode = 0;
    g_ddsFaultAt = 0;
    if (!g_rs7_fn_dds || !g_rs7_fn_nfctor || !g_rs7_fn_nfdtor) {
        return nullptr;
    }

    MemFile file{kMemFileVt, 1, bytes, size, 0};
    alignas(8) uint8_t adapter[kNiFileSize] = {};
    void* res = nullptr;

    __try {
        AdapterCtor(adapter, &file);
        res = reinterpret_cast<void*(__cdecl*)(const char*, void*, void*)>(g_rs7_fn_dds)(key, adapter, nullptr);
        AdapterDtor(adapter);
    } __except (DdsFilter(GetExceptionInformation())) {
        return nullptr;
    }

    return res;
}

bool INameIn(const std::string& n, const std::vector<std::string>& list) {
    for (const std::string& x : list) {
        if (_stricmp(x.c_str(), n.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> StockDdsNames(const void* stockBuf, uint32_t stockSize) {
    return Rs7Asset_DdsNames(static_cast<const uint8_t*>(stockBuf), stockBuf ? stockSize : 0);
}

// Every texture the model names that the stock model does not name and that lies next to the model
// file is registered under "<stock folder>/<name>" before the model is loaded, so the model's
// NiSourceTextures resolve to it like the stock ones resolve to the package's textures. Names the
// stock model shares (the String shader's lookup textures) stay the game's.
int RegisterTextures(const char* name, const std::wstring& path, const std::vector<uint8_t>& model,
                     const void* stockBuf, uint32_t stockSize) {
    const std::vector<std::string> stock = StockDdsNames(stockBuf, stockSize);
    const std::string folder = Rs7Asset_FolderOf(name);
    const std::wstring dir = DirOf(path);
    int n = 0;

    for (const std::string& tex : Rs7Asset_DdsNames(model.data(), model.size())) {
        if (INameIn(tex, stock)) {
            if (Upto(&g_logTex, 24)) {
                Rs7Log("assetload: texture %s: shared with the stock model - the game's", tex.c_str());
            }
            continue;
        }

        std::wstring file = dir;

        file.append(tex.begin(), tex.end());
        std::vector<uint8_t> bytes;

        if (!ReadFileBytes(file, bytes)) {
            if (Upto(&g_logTex, 24)) {
                Rs7Log("assetload: texture %s: no %ls - left to the game (it may render untextured)", tex.c_str(),
                       file.c_str());
            }
            continue;
        }

        if (g_wood < 1.0f && Rs7Asset_IsWoodColour(tex)) {
            const bool ok = Rs7Asset_ScaleDxt1(bytes.data(), bytes.size(), g_wood);

            if (Upto(&g_logTex, 24)) {
                Rs7Log("assetload: texture %s: [assets] headstock7_wood=%.2f %s", tex.c_str(), double(g_wood),
                       ok ? "applied (DXT1 end points scaled)" : "NOT applied (not a plain DXT1 .dds)");
            }
        }

        const std::string key = folder + tex;
        void* res = CallDdsHandler(key.c_str(), bytes.data(), uint32_t(bytes.size()));
        uint8_t* t = res ? Rd<uint8_t*>(res, kResTexture) : nullptr;

        if (t) {
            ++n;
            Rs7Alloc_Inc(kAkAssetTextures);
        }

        if (Upto(&g_logTex, 24)) {
            Rs7Log("assetload: texture %s (%u bytes) registered by the game's dds handler as \"%s\": resource %p, "
                   "texture %p "
                   "(renderer data %p)%s%08lX at %p",
                   tex.c_str(), unsigned(bytes.size()), key.c_str(), res, t, t ? Rd<void*>(t, 0x2C) : nullptr,
                   g_ddsFaultCode ? " - FAULT " : " - fault ", g_ddsFaultCode, reinterpret_cast<void*>(g_ddsFaultAt));
        }
        // The resource is kept (never released), like the merged model's.
    }

    return n;
}

// Diagnostic (once): the engine's NiTexture list entries whose file name contains "rs7hs" (our
// textures): one per texture with renderer data = the model's NiSourceTextures resolved to the
// registered ones; more, or none with renderer data = they did not.
void LogTextureList(const char* when) {
    if (!g_rs7_texlist_cs || !g_rs7_texlist_head) {
        return;
    }

    struct E {
        uintptr_t obj, rd;
        char file[64];
    };

    E found[24];
    int n = 0, nodes = 0;
    auto* cs = reinterpret_cast<CRITICAL_SECTION*>(g_rs7_texlist_cs);

    EnterCriticalSection(cs);
    uintptr_t p = Rd<uintptr_t>(reinterpret_cast<void*>(g_rs7_texlist_head), 0);

    for (; p && nodes < 8192; ++nodes) {
        const char* file = Rd<const char*>(reinterpret_cast<void*>(p), 0x3C);
        char buf[64] = {};

        if (file) {
            rs7s7d::SafeCopy(buf, file, sizeof buf - 1);
        }

        if (std::strstr(buf, "rs7hs") && n < 24) {
            found[n].obj = p;
            found[n].rd = Rd<uintptr_t>(reinterpret_cast<void*>(p), 0x2C);
            std::memcpy(found[n].file, buf, sizeof buf);
            ++n;
        }

        p = Rd<uintptr_t>(reinterpret_cast<void*>(p), 0x34);
    }

    LeaveCriticalSection(cs);
    Rs7Log("assetload: texture list (%s): %d of %d node(s) are rs7hs textures", when, n, nodes);

    for (int i = 0; i < n; ++i) {
        Rs7Log("assetload:   %p \"%s\" renderer data %p", reinterpret_cast<void*>(found[i].obj), found[i].file,
               reinterpret_cast<void*>(found[i].rd));
    }
}
}  // namespace rs7asset
