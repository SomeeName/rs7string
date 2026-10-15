#include "core/assetload.h"
#include "core/allocstats.h"
#include "core/assetdump.h"
#include "core/config.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/hsbuild_io.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <intrin.h>
#include <string>
#include <utility>
#include <vector>

// Threads: Rs7_NifHandler runs on the resource manager's loader threads (Res_WorkerLoopA/B), on a
// master scene graph nothing else has seen yet. Rs7Asset_HeadFrame runs on the game thread (N5).
// The stock path (headstock7=0) never reaches this file: the AL1 stub replays the entry itself.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_asset_on = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_fixstr = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strparams = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_dds = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_nfctor = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_nfdtor = 0;
}

namespace {
using namespace rs7asset;

std::wstring g_dir;  // set by Rs7Asset_Configure (the built-in default is relative to the DLL's folder)
std::wstring g_suffix = kDefaultSuffix;
std::wstring g_file7;    // set by Rs7Asset_Configure; "" = the edited-file rule only
std::wstring g_file7Lh;  // the same for the left-handed in-line frame ([assets] headstock7_file_lh)
// [assets] selfbuild=1 with no usable headstock7_file: the model is built once from the game's own stock
// assets (hsbuild_io.cpp) into g_dir the first time a guitar frame loads, then used as g_file7. Each
// handedness has its own file, switch and state (the left-handed build needs the right-handed textures).
bool g_sbWanted = false, g_sbWantedLh = false;
std::wstring g_sbIni, g_sbDllDir;
volatile long g_sbState = 0, g_sbStateLh = 0;  // 0 untried, 1 running, 2 done
float g_wood = kDefaultWood;  // [assets] headstock7_wood: brightness of the model's wood colour texture
volatile long g_logTex = 0, g_logTexCheck = 0, g_logFrameCheck = 0, g_logSeg7 = 0, g_logPegs = 0, g_logBind = 0,
              g_logSeen = 0, g_logMerged = 0, g_logMissing = 0, g_logFail = 0, g_logFrame = 0, g_logNoAlt = 0,
              g_logLayout = 0;

constexpr uint32_t kFlags = 0x20;       // NiAVObject u16 flags, bit 0 APP_CULLED
constexpr uint32_t kChildren = 0xD8;    // NiNode children: base pointer
constexpr uint32_t kChildSlots = 0xDC;  // u16 slot count (max size)
constexpr uint32_t kName = 0x10;        // NiObjectNET name (NiFixedString handle = char*)

template <class T>
T Rd(const void* base, uint32_t off) {
    T v{};

    if (base) {
        rs7s7d::SafeCopy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    }

    return v;
}

bool Upto(volatile long* n, long max) {
    return _InterlockedIncrement(n) <= max;
}

void SetCulled(uint8_t* node, bool culled) {
    uint16_t f = Rd<uint16_t>(node, kFlags);
    const uint16_t want = uint16_t(culled ? (f | 1u) : (f & ~1u));

    if (want != f) {
        rs7s7d::SafeStore(node + kFlags, &want, sizeof want);
    }
}

uint8_t* TopObject(const void* res) {
    uint8_t* stream = Rd<uint8_t*>(res, kResStream);
    if (!stream || Rd<uint32_t>(stream, kStreamTopCount) < 1) {
        return nullptr;
    }

    uint8_t** base = Rd<uint8_t**>(stream, kStreamTop);

    return base ? Rd<uint8_t*>(base, 0) : nullptr;
}

// Game calls, each SEH-guarded (a fault leaves the stock model alone).
const char* MakeFixedString(const char* text) {  // 0x91CBF0, cdecl(const char*) -> handle (+1 ref)
    if (!g_rs7_fn_fixstr) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<const char*(__cdecl*)(const char*)>(g_rs7_fn_fixstr)(text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool AttachChild(uint8_t* parent, uint8_t* child) {  // NiNode vtbl+0A0h, thiscall(child, bFirstAvail)
    __try {
        void* const* vt = *reinterpret_cast<void* const* const*>(parent);
        using Fn = void(__fastcall*)(uint8_t*, void*, uint8_t*, uint32_t);

        reinterpret_cast<Fn>(vt[kNodeAttach / 4])(parent, nullptr, child, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* CallStock(const char* name, const void* buf, uint32_t size, void* ctx) {
    __try {
        return Rs7_NifHandlerStock(name, buf, size, ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

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

// Loads the edited file with the stock handler (same resource name) and hangs its top object,
// renamed and culled, under the stock top object. The edited resource is kept (never released):
// its NiStream holds the only other references and the master lives as long as the game.
void Merge(const char* name, void* res, const std::wstring& path, const std::vector<uint8_t>& bytes) {
    uint8_t* top = TopObject(res);

    if (!top || !rs7s7d::IsKindOf(top, "NiNode")) {
        if (Upto(&g_logFail, 8)) {
            Rs7Log("assetload: %s: stock top object %p is not an NiNode - not merged", name, top);
        }
        return;
    }

    void* res2 = CallStock(name, bytes.data(), uint32_t(bytes.size()), nullptr);
    uint8_t* alt = res2 ? TopObject(res2) : nullptr;

    if (!alt || !rs7s7d::IsKindOf(alt, "NiNode") || Rd<uint8_t*>(alt, kParent)) {
        if (Upto(&g_logFail, 8)) {
            Rs7Log("assetload: %s: %ls did not load as a scene (resource %p, top %p) - stock model only", name,
                   path.c_str(), res2, alt);
        }
        return;
    }

    char oldName[48] = {};

    rs7s7d::NameOfNode(alt, oldName, sizeof oldName);
    if (const char* fs = MakeFixedString(kAltName)) {
        rs7s7d::SafeStore(alt + kName, &fs, sizeof fs);  // old ref leaked
    }
    SetCulled(alt, true);  // every clone starts with the stock model shown
    const unsigned before = Rd<uint16_t>(top, kChildSlots);

    if (!AttachChild(top, alt)) {
        if (Upto(&g_logFail, 8)) {
            Rs7Log("assetload: %s: AttachChild faulted - stock model only", name);
        }
        return;
    }

    Rs7Alloc_Inc(kAkAssetMerges);
    char topName[48] = {}, newName[48] = {};

    rs7s7d::NameOfNode(top, topName, sizeof topName);
    rs7s7d::NameOfNode(alt, newName, sizeof newName);

    if (Upto(&g_logMerged, 16)) {
        Rs7Log("assetload: %s: merged %ls (%u bytes): its top \"%s\" %p -> \"%s\", culled, attached under the stock "
               "top \"%s\" %p (child slots %u -> %u, parent now %p)",
               name, path.c_str(), unsigned(bytes.size()), oldName, alt, newName, topName, top, before,
               unsigned(Rd<uint16_t>(top, kChildSlots)), Rd<void*>(alt, kParent));
    }
}

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

std::wstring DirOf(const std::wstring& path) {
    const size_t k = path.find_last_of(L"\\/");

    return k == std::wstring::npos ? std::wstring() : path.substr(0, k + 1);
}

// Every texture the model names that the stock model does not name and that lies next to the model
// file is registered under "<stock folder>/<name>" before the model is loaded, so the model's
// NiSourceTextures resolve to it like the stock ones resolve to the package's textures. Names the
// stock model shares (the String shader's lookup textures) stay the game's.
int RegisterTextures(const char* name, const std::wstring& path, const std::vector<uint8_t>& model,
                     const void* stockBuf, uint32_t stockSize) {
    const std::vector<std::string> stock =
        Rs7Asset_DdsNames(static_cast<const uint8_t*>(stockBuf), stockBuf ? stockSize : 0);
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

// ---- per guitar (game thread) ----
constexpr int kSegs = 6;  // stock headstock string segments (param objects Rnd_Headstock +20h + s*4)
constexpr uint32_t kHeadParams = 0x20;
constexpr uint32_t kColourRgb =
    0x14;  // g_diffuseColor extra data: RGB floats (Rnd_Mesh_SetDiffuseColorExtraData 0x7AF860)

struct Head {
    uint8_t* guitar;
    uint8_t* root;     // frame entity root the alt was found under
    uint8_t* alt;      // the rs7_alt clone (nullptr: none in this frame)
    uint8_t* sibs[8];  // stock siblings we culled
    int nsibs;
    bool applied;  // alt shown
    bool bound;    // the param objects point at the alt's segments
    int frames;
    uint8_t* stockSeg[kSegs];       // slot s -> string_size_<6-s> of the stock model
    uint8_t* altSeg[kSegs];         // the same names in rs7_alt
    uint8_t* seg7;                  // rs7_alt's string_size_7 (no stock slot)
    uintptr_t params7[9];           // our own param holder for seg7 (same layout as the stock ones)
    uintptr_t altHold[kSegs][9];    // the alt segments' extra data (our holders)
    uintptr_t stockHold[kSegs][9];  // the stock holders' pointers (one reference each, ours)
    bool segOk[kSegs];
    bool pointed;              // the stock param objects point at the alt's extra data
    uint8_t* stockLoc[kSegs];  // PegSet slot s: the stock Locator_Peg_<s+1> (or _Invert_) node
    uint8_t* altLoc[kSegs];    // the same names in rs7_alt
    uint8_t* altLoc7;          // rs7_alt's Locator_Peg_7 (or Locator_Invert_Peg_7): PegSet slot 6 (peg 7)
    bool locSwapped;
    bool texChecked;
    bool onNonInline;                      // the frame is not the in-line one (InstrumentInfo +0Ah = 0, 3+3): LT1, pegs
    uint8_t* pegDone[rs7head::kCapacity];  // peg entities whose clone shows the merged in-line peg (3+3)
    uint8_t* pegCheckedSet;  // a stock guitar's PegSet whose pegs were checked for a left-over in-line peg
};

const int kInfoGuitar[4] = {1, 0, 0, 0};  // Rnd_SetStringShaderParams only reads info+0 (2 = bass)
Head g_heads[8];

uint8_t* FindAlt(uint8_t* root) {  // breadth-first, first 3 levels
    uint8_t* q[64];
    int head = 0, tail = 0;

    q[tail++] = root;

    for (int depth = 0; depth < 3 && head < tail; ++depth) {
        const int end = tail;

        for (; head < end; ++head) {
            uint8_t* n = q[head];
            if (!rs7s7d::IsKindOf(n, "NiNode")) {
                continue;
            }

            const unsigned slots = Rd<uint16_t>(n, kChildSlots);
            uint8_t** kids = Rd<uint8_t**>(n, kChildren);

            for (unsigned i = 0; kids && i < slots && i < 32; ++i) {
                uint8_t* c = Rd<uint8_t*>(kids, 4 * i);
                if (!c) {
                    continue;
                }

                char name[16] = {};

                rs7s7d::NameOfNode(c, name, sizeof name);
                if (!std::strcmp(name, kAltName)) {
                    return c;
                }

                if (tail < 64) {
                    q[tail++] = c;
                }
            }
        }
    }

    return nullptr;
}

uint8_t* FindByName(uint8_t* root, const char* want) {  // depth-first, at most 256 nodes
    uint8_t* stack[64];
    int sp = 0, seen = 0;

    stack[sp++] = root;

    while (sp && seen++ < 256) {
        uint8_t* n = stack[--sp];
        char name[48] = {};

        rs7s7d::NameOfNode(n, name, sizeof name);
        if (!std::strcmp(name, want)) {
            return n;
        }
        if (!rs7s7d::IsKindOf(n, "NiNode")) {
            continue;
        }

        const unsigned slots = Rd<uint16_t>(n, kChildSlots);
        uint8_t** kids = Rd<uint8_t**>(n, kChildren);

        for (int i = int(slots < 48 ? slots : 48) - 1; kids && i >= 0; --i) {
            uint8_t* c = Rd<uint8_t*>(kids, 4 * uint32_t(i));

            if (c && sp < 64) {
                stack[sp++] = c;
            }
        }
    }

    return nullptr;
}

// Rnd_SetStringShaderParams 0x5998F0 (EAX = param object, cdecl (info, slot, mesh); it releases the
// mesh once, so it is AddRef'd first, as 0x58E2BB..0x58E2C1 does): sets the segment's shader extra
// data and points the param object at it, so the game's per-frame segment colour reaches it.
bool BindSegment(void* params, uint8_t* mesh, uint32_t slot) {
    if (!g_rs7_fn_strparams || !mesh || !rs7s7d::IsKindOf(mesh, "NiMesh")) {
        return false;
    }

    __try {
        _InterlockedIncrement(reinterpret_cast<volatile long*>(mesh + 4));
        const int* info = kInfoGuitar;

        __asm {
            push mesh
            push slot
            push info
            mov eax, params
            call dword ptr [g_rs7_fn_strparams]
            add esp, 0Ch
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Param object fields (Rnd_SetStringShaderParams): 0 g_flickerPhase, 1 g_flickerFade,
// 2 g_sharpToBlurLerpVal, 3 g_diffuseColor (RGBA +14h..+23h), 4 v_positionString,
// 5 g_vertexWidthScale, 6 g_glowScale, 7 g_glowPow, 8 g_glowBrightness (floats at +14h).
uint32_t ValueSize(int field) {
    return field == 3 ? 16u : 4u;
}

void SwapRef(uintptr_t* slot, uintptr_t now) {  // NiPointer-style assignment, the old one stays alive
    const uintptr_t old = *slot;
    if (old == now || !now) {
        return;
    }

    _InterlockedIncrement(reinterpret_cast<volatile long*>(now + 4));
    *slot = now;
    if (old) {
        _InterlockedDecrement(reinterpret_cast<volatile long*>(old + 4));
    }
}

// Once per frame instance: our own param holders for the alt's segments (the stock holders are not
// touched yet) and a referenced copy of the stock holders' pointers.
void BindAlt(Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);
    int n = 0;

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = Rd<uintptr_t*>(head, kHeadParams + 4u * uint32_t(s));

        h.segOk[s] = false;
        if (!params || !h.altSeg[s]) {
            continue;
        }

        std::memset(h.altHold[s], 0, sizeof h.altHold[s]);
        if (!BindSegment(h.altHold[s], h.altSeg[s], uint32_t(s))) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            h.stockHold[s][i] = 0;
            uintptr_t p = 0;
            if (!rs7s7d::SafeCopy(&p, params + i, sizeof p) || !p || !h.altHold[s][i]) {
                continue;
            }

            _InterlockedIncrement(reinterpret_cast<volatile long*>(p + 4));
            h.stockHold[s][i] = p;
        }

        h.segOk[s] = true;
        ++n;
    }

    if (h.seg7) {
        BindSegment(h.params7, h.seg7, 0);  // string 0's V row, like the 7th string line
    }
    h.bound = true;
    Rs7Alloc_Inc(kAkAssetBinds);
    if (Upto(&g_logBind, 8)) {
        Rs7Log("assetload: guitar %p: %d of %d headstock string segment(s) of the edited model bound%s", h.guitar, n,
               kSegs, h.seg7 ? "; string_size_7 bound to its own params" : "; no string_size_7");
    }
}

// The game's per-frame segment state goes through the six stock param objects: point them at the
// alt's extra data (toAlt) or back, carrying the current values over, so nothing is reset.
void PointParams(Head& h, bool toAlt) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);

    for (int s = 0; head && s < kSegs; ++s) {
        uintptr_t* params = Rd<uintptr_t*>(head, kHeadParams + 4u * uint32_t(s));
        if (!params || !h.segOk[s]) {
            continue;
        }

        for (int i = 0; i < 9; ++i) {
            const uintptr_t from = toAlt ? h.stockHold[s][i] : h.altHold[s][i];
            const uintptr_t to = toAlt ? h.altHold[s][i] : h.stockHold[s][i];
            if (!from || !to) {
                continue;
            }

            uint8_t v[16];

            if (rs7s7d::SafeCopy(v, reinterpret_cast<uint8_t*>(from) + kColourRgb, ValueSize(i))) {
                rs7s7d::SafeStore(reinterpret_cast<uint8_t*>(to) + kColourRgb, v, ValueSize(i));
            }
            SwapRef(params + i, to);
        }
    }

    h.pointed = toAlt;
}

void FindSegments(Head& h) {
    // The stock model: the first NiNode sibling of rs7_alt (culled or not, so the restore always finds it).
    uint8_t* stock = nullptr;
    uint8_t* parent = Rd<uint8_t*>(h.alt, kParent);
    const unsigned slots = parent ? Rd<uint16_t>(parent, kChildSlots) : 0;
    uint8_t** kids = parent ? Rd<uint8_t**>(parent, kChildren) : nullptr;

    for (unsigned i = 0; kids && i < slots && i < 32 && !stock; ++i) {
        uint8_t* c = Rd<uint8_t*>(kids, 4 * i);

        if (c && c != h.alt && rs7s7d::IsKindOf(c, "NiNode")) {
            stock = c;
        }
    }

    for (int s = 0; s < kSegs; ++s) {
        char name[24];

        std::snprintf(name, sizeof name, "string_size_%d", kSegs - s);
        h.altSeg[s] = FindByName(h.alt, name);
        h.stockSeg[s] = stock ? FindByName(stock, name) : nullptr;
    }

    h.seg7 = FindByName(h.alt, "string_size_7");
}

// ---- pegs: the PegSet slots' locator pointers (slot +8, an NiPointer, Rnd_PegSet_BindLocators
// 0x59C980) move to the alt's Locator_Peg_<s+1>, so the stock Rnd_PegSet_ApplyTransforms 0x59C630
// and our per-frame placement put the pegs (and their tuner letters) on the edited model. Slot 6
// (peg 7 of the native PegSet, headstock.h) was bound by the stock binder itself: Locator_Peg_7
// exists only in rs7_alt, so the frame tree's name lookup finds it there; it is checked here too.
constexpr float kRadToDegNeg = -57.2957763671875f;  // as 0x59C67D..0x59C6A5

uint8_t* PegSet(const Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);

    return head ? Rd<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;
}

uint8_t* PegSlot(uint8_t* pegset, int s) {
    return pegset + rs7head::kSlots + uint32_t(s) * rs7head::kSlotStride;
}

void FindLocators(Head& h) {
    uint8_t* info = Rd<uint8_t*>(h.guitar, 0x24);
    // InstrumentInfo +8 = InvertGuitarStrings (CONFIRMED Rnd_InstrumentInfo_FromProfile 0x58ABDB: profile +12h),
    // the byte the stock binder 0x59C980 picks Locator_Invert_Peg_%d by (0x59CA2B); +9 lefty, +0Ah in-line.
    const bool invert = info && Rd<uint8_t>(info, 8) != 0;

    for (int s = 0; s < kSegs; ++s) {
        char name[32];

        std::snprintf(name, sizeof name, invert ? "Locator_Invert_Peg_%d" : "Locator_Peg_%d", s + 1);
        h.altLoc[s] = FindByName(h.alt, name);
        h.stockLoc[s] = nullptr;
    }

    h.altLoc7 = FindByName(h.alt, invert ? "Locator_Invert_Peg_7" : "Locator_Peg_7");
}

void SetLocator(uint8_t* slot, uint8_t* node) {  // NiPointer assignment, as 0x59CAD0..
    uint8_t* cur = Rd<uint8_t*>(slot, rs7head::kSlotLocator);
    if (!node || cur == node) {
        return;
    }

    _InterlockedIncrement(reinterpret_cast<volatile long*>(node + 4));
    rs7s7d::SafeStore(slot + rs7head::kSlotLocator, &node, sizeof node);
    if (cur) {
        _InterlockedDecrement(reinterpret_cast<volatile long*>(cur + 4));  // still held by its tree
    }
}

void SwapLocators(Head& h, bool toAlt) {
    uint8_t* pegset = PegSet(h);
    if (!pegset) {
        return;
    }

    const unsigned count = Rd<uint16_t>(pegset, rs7head::kPegCount);
    int n = 0;

    if (count > unsigned(kSegs) && h.altLoc7 && Rd<uint8_t>(PegSlot(pegset, kSegs), 0)) {
        SetLocator(PegSlot(pegset, kSegs), h.altLoc7);  // the binder's own choice; kept on the alt either way
        ++n;
    }

    for (int s = 0; s < kSegs && s < int(count); ++s) {
        uint8_t* slot = PegSlot(pegset, s);
        if (!Rd<uint8_t>(slot, 0)) {
            continue;
        }

        if (toAlt) {
            if (!h.altLoc[s]) {
                continue;
            }

            if (!h.stockLoc[s]) {
                h.stockLoc[s] = Rd<uint8_t*>(slot, rs7head::kSlotLocator);
            }
            SetLocator(slot, h.altLoc[s]);
        } else {
            SetLocator(slot, h.stockLoc[s]);
        }

        ++n;
    }

    h.locSwapped = toAlt;

    if (Upto(&g_logPegs, 8)) {
        float w7[3] = {};

        if (toAlt && h.altLoc7) {
            rs7s7d::SafeCopy(w7, h.altLoc7 + 0xA4, sizeof w7);
        }
        Rs7Log("assetload: guitar %p: %d peg locator(s) of PegSet %p moved to the %s model%s (%.3f, %.3f, %.3f)",
               h.guitar, n, pegset, toAlt ? "7-string" : "stock",
               toAlt ? (h.altLoc7 ? "; its Locator_Peg_7 (the 7th peg, the B letter) at world"
                                  : "; it has NO Locator_Peg_7")
                     : "",
               double(w7[0]), double(w7[1]), double(w7[2]));
    }
}

void GameEuler(const uint8_t* rot, float* e) {  // 0x8FCAA0, as headstock.cpp
    float* e0 = e;
    float* e1 = e + 1;
    float* e2 = e + 2;

    __asm {
        push ebx
        push esi
        push edi
        mov esi, rot
        mov edi, e2
        push e1
        push e0
        call dword ptr [g_rs7_fn_euler]
        pop edi
        pop esi
        pop ebx
    }

    for (int i = 0; i < 3; ++i) e[i] *= kRadToDegNeg;
}

bool PlacePegsGuarded(uint8_t* pegset, unsigned count) {  // what 0x59C630 does per slot
    __try {
        for (unsigned s = 0; s < count && s < unsigned(rs7head::kCapacity); ++s) {
            uint8_t* slot = PegSlot(pegset, int(s));
            uint8_t* peg = *reinterpret_cast<uint8_t**>(slot + rs7head::kSlotEntity);
            uint8_t* loc = *reinterpret_cast<uint8_t**>(slot + rs7head::kSlotLocator);
            if (!peg || !loc || !*slot) {
                continue;
            }

            float e[3] = {};

            GameEuler(loc + 0x80, e);
            reinterpret_cast<void(__cdecl*)(uint8_t*, const float*)>(g_rs7_fn_setpos)(
                peg, reinterpret_cast<float*>(loc + 0xA4));
            reinterpret_cast<void(__cdecl*)(uint8_t*, const float*)>(g_rs7_fn_setrot)(peg, e);
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PlacePegs(Head& h) {
    uint8_t* pegset = PegSet(h);
    if (!pegset || !g_rs7_fn_setpos || !g_rs7_fn_setrot || !g_rs7_fn_euler) {
        return;
    }
    if (!Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    PlacePegsGuarded(pegset, Rd<uint16_t>(pegset, rs7head::kPegCount));
}

// The 7th segment's colour, every shown frame: what Rnd_Headstock_UpdateSegmentColours 0x58E4F0
// does for segment s < stringCount (string object +50h base shape found: copy the colour handle
// +38h's RGBA (+14h..+23h) onto the segment's g_diffuseColor), for the 7th string's object S7. So it
// is lit / dimmed exactly like the 7th string line (N11). Without S7 ([neck] string7_line=0) the
// static [color] string6 colour.
constexpr uint32_t kStrShapeFound = 0x50;
constexpr uint32_t kStrColour = 0x38;

void Colour7(Head& h) {
    uint8_t* cd = reinterpret_cast<uint8_t*>(h.params7[3]);
    if (!cd) {
        return;
    }

    uint8_t* s7 = Rs7S7_ObjectOf(h.guitar);
    uint8_t* src = (s7 && Rd<uint8_t>(s7, kStrShapeFound)) ? Rd<uint8_t*>(s7, kStrColour) : nullptr;
    uint8_t rgba[16];

    if (src && rs7s7d::SafeCopy(rgba, src + kColourRgb, sizeof rgba)) {
        rs7s7d::SafeStore(cd + kColourRgb, rgba, sizeof rgba);
        if (Upto(&g_logSeg7, 2)) {
            Rs7Log("assetload: guitar %p: string_size_7 follows the 7th string's colour (S7 %p colour %p)", h.guitar,
                   s7, src);
        }
    } else {
        rs7s7d::SafeStore(cd + kColourRgb, g_rs7_fx_rgb, 3 * sizeof(float));
    }
}

// The 7th segment at the nut (issue #17 / #23 / nut gap, 2026-10-15). string_size_7 is left exactly as
// the model has it: its nut end equals string_size_1..6's (model y 15.21, same height and radius,
// pitch 1.608 in z) and lands on the 7th neck string's start (Rs7Gs_AttachExt, gsnative.h
// ExtrapolateAttach: z to 0.001, y to 0.011), like strings 0-5 meet theirs. An older workaround
// sheared the segment's nut end 0.30 deeper (x) on the idea that the 7th nut slot is lower; the real
// cause of "the segment draws in front of the nut" was the draw layer (Layer7 below, CONFIRMED in game
// by issue #23), so the shear only left the thick segment end 0.30 off the neck string's axis, more
// than the end radius (0.20): the visible gap at the nut. It is gone.

// Issue #23: string_size_7 gets string_size_6's noteway draw layer (11 in stock), every frame while
// shown (a cheap walk of two property lists; only a difference is written).
volatile long g_logLayer = 0;

void Layer7(Head& h) {
    const uint8_t* from = h.altSeg[0] ? h.altSeg[0] : h.stockSeg[0];  // string_size_6
    uint16_t value = 0, was = 0;
    const int rc = rs7asset::CopyLayer(from, h.seg7, rs7asset::GamePropType, &value, &was);

    if (rc == 1 && Upto(&g_logLayer, 2)) {
        Rs7Log("assetload: guitar %p: string_size_7 draw layer %u -> %u (string_size_6's; it drew after the fret "
               "bars, over the nut - issue #23)",
               h.guitar, unsigned(was), unsigned(value));
    } else if (rc < 0 && Upto(&g_logLayer, 2)) {
        Rs7Log("assetload: guitar %p: string_size_7 draw layer NOT set (no layer property on string_size_%s)", h.guitar,
               rs7asset::FindProperty(from, rs7asset::kLayerType, rs7asset::GamePropType) ? "7" : "6");
    }
}

// ---- 3+3: the in-line peg in a 7-string guitar's peg clones (kLayouts comment) ----
// Each peg is a pool entity with its own clone of peg_bakelite.nif, which carries the merged in-line peg as
// rs7_alt (culled). Swapped: the bakelite parts culled, rs7_alt shown, the bakelite locator_letter /
// Locator_Arrow renamed (the game looks them up by name, depth-first: UI_PlayerTuner_PlacePegLetters 0x5B1A10
// every frame, the precision widget 0x5B4DE0), so the lookups find rs7_alt's. Undone the same way, also for a
// recycled clone that a stock guitar gets later (StockPegs).
constexpr const char* kLetter = "locator_letter";
constexpr const char* kArrow = "Locator_Arrow";
constexpr const char* kLetterOff = "rs7s_locator_letter";
constexpr const char* kArrowOff = "rs7s_Locator_Arrow";
volatile long g_pegSwapEver = 0, g_logPegAlt = 0;

void Rename(uint8_t* node, const char* to) {  // the node's old name reference is leaked (as Merge does)
    if (!node) {
        return;
    }

    if (const char* fs = MakeFixedString(to)) {
        rs7s7d::SafeStore(node + kName, &fs, sizeof fs);
    }
}

uint8_t* FindOutside(uint8_t* root, uint8_t* alt, const char* want) {  // in root's children except alt's subtree
    const unsigned slots = Rd<uint16_t>(root, kChildSlots);
    uint8_t** kids = Rd<uint8_t**>(root, kChildren);

    for (unsigned i = 0; kids && i < slots && i < 32; ++i) {
        uint8_t* c = Rd<uint8_t*>(kids, 4 * i);
        if (!c || c == alt) {
            continue;
        }
        if (uint8_t* n = FindByName(c, want)) {
            return n;
        }
    }

    return nullptr;
}

// show = true: in-line peg; false: the option's own (stock) peg. Returns whether the clone has an rs7_alt.
bool PegShowInline(uint8_t* entity, bool show) {
    uint8_t* eroot = entity ? rs7s7d::EntityRoot(entity) : nullptr;
    uint8_t* alt = eroot ? FindAlt(eroot) : nullptr;
    uint8_t* root = alt ? Rd<uint8_t*>(alt, kParent) : nullptr;  // the clone's top object ("Scene Root")
    if (!root) {
        return false;
    }

    const unsigned slots = Rd<uint16_t>(root, kChildSlots);
    uint8_t** kids = Rd<uint8_t**>(root, kChildren);

    for (unsigned i = 0; kids && i < slots && i < 32; ++i) {
        uint8_t* c = Rd<uint8_t*>(kids, 4 * i);
        if (!c || c == alt) {
            continue;
        }
        if (!rs7s7d::IsKindOf(c, "NiNode") && !rs7s7d::IsKindOf(c, "NiMesh")) {
            continue;
        }

        char nm[32] = {};

        rs7s7d::NameOfNode(c, nm, sizeof nm);
        if (!std::strcmp(nm, kLetter) || !std::strcmp(nm, kArrow) || !std::strcmp(nm, kLetterOff) ||
            !std::strcmp(nm, kArrowOff)) {
            continue;  // locators: renamed, never culled
        }

        SetCulled(c, show);
    }

    SetCulled(alt, !show);
    Rename(FindOutside(root, alt, show ? kLetter : kLetterOff), show ? kLetterOff : kLetter);
    Rename(FindOutside(root, alt, show ? kArrow : kArrowOff), show ? kArrowOff : kArrow);

    return true;
}

void InlinePegs(Head& h) {  // every frame while shown on a non-in-line frame; cheap once done
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);
    uint8_t* pegset = head ? Rd<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;
    if (!pegset || !Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    int n = 0, swapped = 0;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = PegSlot(pegset, s);
        uint8_t* e = Rd<uint8_t>(slot, 0) ? Rd<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;
        if (!e || h.pegDone[s] == e) {
            continue;
        }

        ++n;

        if (PegShowInline(e, true)) {
            h.pegDone[s] = e;
            ++swapped;
            _InterlockedExchange(&g_pegSwapEver, 1);
        } else {
            h.pegDone[s] = e;  // no merged peg in this clone: leave it (logged once)
        }
    }

    if (n && Upto(&g_logPegAlt, 6)) {
        Rs7Log("assetload: guitar %p (non-in-line frame, 3+3): %d of %d peg(s) of PegSet %p show the in-line peg%s",
               h.guitar, swapped, n, pegset,
               swapped ? ""
                       : " - none: peg_bakelite.nif has no merged rs7_alt (see the "
                         "assetload peg line at its load)");
    }
}

void RestorePegs(Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);
    uint8_t* pegset = head ? Rd<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = pegset ? PegSlot(pegset, s) : nullptr;
        uint8_t* e = slot ? Rd<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;

        if (e && h.pegDone[s] == e) {
            PegShowInline(e, false);
        }
        h.pegDone[s] = nullptr;
    }
}

// A stock guitar (not shown with the 7-string model): once per loaded PegSet, any peg clone still showing the
// in-line peg (a pool instance recycled from a 7-string guitar) gets its own peg back. Only after a swap ever.
void StockPegs(Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, 0x18);
    uint8_t* pegset = head ? Rd<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;
    if (!pegset || pegset == h.pegCheckedSet || !Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    h.pegCheckedSet = pegset;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = PegSlot(pegset, s);
        uint8_t* e = Rd<uint8_t*>(slot, rs7head::kSlotEntity);
        uint8_t* root = e ? rs7s7d::EntityRoot(e) : nullptr;
        uint8_t* alt = root ? FindAlt(root) : nullptr;

        if (alt && !(Rd<uint16_t>(alt, kFlags) & 1)) {
            PegShowInline(e, false);
        }
    }
}

Head* Slot(uint8_t* guitar) {
    for (Head& h : g_heads) {
        if (h.guitar == guitar) {
            return &h;
        }
    }

    for (Head& h : g_heads) {
        if (!h.guitar) {
            h = Head{};
            h.guitar = guitar;
            Rs7Alloc_Inc(kAkHeadSlots);
            return &h;
        }
    }

    return nullptr;
}

void Restore(Head& h) {
    if (h.pointed) {
        PointParams(h, false);
    }

    if (h.locSwapped) {
        SwapLocators(h, false);
        PlacePegs(h);
    }

    for (int i = 0; i < h.nsibs; ++i) {
        SetCulled(h.sibs[i], false);
    }

    h.nsibs = 0;
    if (h.alt) {
        SetCulled(h.alt, true);
    }
    if (h.onNonInline) {
        RestorePegs(h);
    }
    h.onNonInline = false;
    h.applied = false;
}

void Show(Head& h) {  // every frame while wanted: alt shown, its stock siblings culled
    uint8_t* parent = Rd<uint8_t*>(h.alt, kParent);
    const unsigned slots = parent ? Rd<uint16_t>(parent, kChildSlots) : 0;
    uint8_t** kids = parent ? Rd<uint8_t**>(parent, kChildren) : nullptr;

    for (unsigned i = 0; kids && i < slots && i < 32; ++i) {
        uint8_t* c = Rd<uint8_t*>(kids, 4 * i);
        if (!c || c == h.alt) {
            continue;
        }
        if (!rs7s7d::IsKindOf(c, "NiNode") && !rs7s7d::IsKindOf(c, "NiMesh")) {
            continue;  // effects stay
        }
        if (Rd<uint16_t>(c, kFlags) & 1) {
            continue;  // already culled (by the game or by us)
        }

        bool known = false;

        for (int k = 0; k < h.nsibs; ++k) {
            known = known || h.sibs[k] == c;
        }

        if (!known && h.nsibs < 8) {
            h.sibs[h.nsibs++] = c;
        }
        SetCulled(c, true);
    }

    SetCulled(h.alt, false);
    h.applied = true;

    if (!h.bound) {
        FindSegments(h);
        FindLocators(h);
        BindAlt(h);
    }

    if (!h.pointed) {
        PointParams(h, true);
    }
    if (!h.locSwapped) {
        SwapLocators(h, true);
    }
    if (h.locSwapped) {
        PlacePegs(h);
    }
    uint8_t* info = Rd<uint8_t*>(h.guitar, 0x24);

    h.onNonInline = info && Rd<uint8_t>(info, 0x0A) == 0;  // InstrumentInfo +0Ah in-line (CONFIRMED 0x58ABEC)
    if (h.onNonInline) {
        InlinePegs(h);
    }
    // Native strings: the stock segment colour loop does it for string 6 (GSHC, core/gsnative.h).
    if (h.seg7 && !(g_rs7_native_strings && rs7gs::IsNative(Rd<uint8_t*>(h.guitar, 0x0C)))) {
        Colour7(h);
    }
    if (h.seg7) {
        Layer7(h);
    }

    if (!h.texChecked) {
        h.texChecked = true;
        if (Upto(&g_logFrameCheck, 1)) {
            LogTextureList("first show");
        }
    }
}
}  // namespace

int rs7asset::GamePropType(const uint8_t* prop) {
    if (!prop) {
        return -1;
    }

    using TypeFn = int(__thiscall*)(const uint8_t*);

    __try {
        const uintptr_t vt = *reinterpret_cast<const uintptr_t*>(prop);

        return (*reinterpret_cast<const TypeFn*>(vt + 0x48))(prop);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

uint8_t* rs7asset::FindProperty(const uint8_t* obj, int type, PropTypeFn typeOf) {
    if (!obj || !typeOf) {
        return nullptr;
    }

    __try {
        const uint8_t* node = *reinterpret_cast<const uint8_t* const*>(obj + kPropList);

        for (int i = 0; node && i < 64; ++i) {
            uint8_t* prop = *reinterpret_cast<uint8_t* const*>(node + 8);
            if (prop && typeOf(prop) == type) {
                return prop;
            }

            node = *reinterpret_cast<const uint8_t* const*>(node);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return nullptr;
}

int rs7asset::CopyLayer(const uint8_t* from, uint8_t* to, PropTypeFn typeOf, uint16_t* value, uint16_t* was) {
    const uint8_t* src = FindProperty(from, kLayerType, typeOf);
    uint8_t* dst = FindProperty(to, kLayerType, typeOf);
    uint16_t v = 0, old = 0;

    if (src && !rs7s7d::SafeCopy(&v, src + kLayerValue, sizeof v)) {
        src = nullptr;
    }
    if (src && value) {
        *value = v;
    }
    if (!src || !dst || !rs7s7d::SafeCopy(&old, dst + kLayerValue, sizeof old)) {
        return -1;
    }
    if (old == v) {
        return 0;
    }
    if (!rs7s7d::SafeStore(dst + kLayerValue, &v, sizeof v)) {
        return -1;
    }

    if (was) {
        *was = old;
    }

    return 1;
}

std::wstring Rs7Asset_EditedPath(const char* name, const std::wstring& dir, const std::wstring& suffix) {
    if (!name || dir.empty()) {
        return {};
    }

    const size_t len = std::strlen(name);
    if (len < 5 || len > 240 || _stricmp(name + len - 4, ".nif") != 0) {
        return {};
    }

    std::string low(name, len);

    for (char& ch : low) {
        ch = char((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    }

    for (char& ch : low) {
        ch = ch == '\\' ? '/' : ch;
    }

    if (low.find("/gameplay/guitar") == std::string::npos) {
        return {};
    }

    const size_t slash = low.find_last_of('/');
    const std::string base = low.substr(slash + 1, low.size() - slash - 1 - 4);
    if (base.empty()) {
        return {};
    }

    for (char ch : base) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
            return {};
        }
    }

    if (base.find("..") != std::string::npos) {
        return {};
    }

    std::wstring out = dir;

    if (out.back() != L'\\' && out.back() != L'/') {
        out += L'\\';
    }
    out.append(base.begin(), base.end());
    out += suffix;
    out += L".nif";

    return out;
}

// The layouts the 7-string in-line model (headstock7_file / headstock7_file_lh) replaces. One row per
// value of Optionen > Spieleinstellungen > Gitarrenkopfplatte; the game loads exactly one frame per
// handedness from the row's folder. CONFIRMED (asset dump of the game's own packages + run logs):
//   "6 in Reihe": base\dependency_guitarstandardinlined{rh,lh}.psarc, guitar_standard_inline/
//                 epi_wilshire_cherry.nif / epi_whilshire_cherry_lh.nif, stock part Wilshire_RH / Wilshire_LH
//   "3+3":        base\dependency_guitarstandard3x3{,lh}.psarc, guitar_standard/ gib_lespaul.nif /
//                 gib_lespaul_lh.nif, stock part LP_RH / LP_LH; pegs base\dependency_guitarstandard3x3peg.psarc
//                 guitar_standard/peg_bakelite.nif (one model for both hands)
// Attach transform: none for any row. Both models hang under an identity "Scene Root" and carry their
// geometry in that space; the nut (strings start at x = 0, z = 53.6..53.7 in glTF axes) and the string
// spans (+-5.5) coincide between Wilshire_RH and LP_RH (and the LH twins, mirrored in y), so the
// in-line model sits on a 3+3 neck exactly where it sits on an in-line one. The stock parts are
// culled by Show() as "every stock NiNode/NiMesh sibling of rs7_alt", not by name; stockRh/stockLh
// only document what that is (and are logged with the layout).
// 3+3 pegs and letters (fix/headstock2): the first 3+3 attempt (tests/shots/hs33/t4_33_closeup.png) showed
// bakelite pegs and the letters G B e B turned round. The letters are the stock rule of
// UI_PlayerTuner_PlacePegLetters 0x5B1A10 (CONFIRMED 0x5B1B53..0x5B1B7D: in-line byte InstrumentInfo +0Ah = 0
// and not inverted -> the letters of pegs >= count/2 turned round; 7 pegs -> indices 3..6 = G B e B, exactly
// the shot): LT1 reports such a guitar as in-line. The pegs: the game picks the peg pool model by the profile
// option (Rnd_Guitar_LoadDependencies 0x5965A0), so peg_bakelite.nif gets the in-line peg
// (epi_whilshire_peg.nif + wilshire_hardware.dds from the in-line package, the game's own reader) merged in as
// rs7_alt, culled; Show() swaps it in per peg of a guitar that shows the 7-string headstock, and the bakelite
// locator_letter / Locator_Arrow of those pegs are renamed so the game's name lookups find the in-line ones.
const HeadLayout kLayouts[] = {
    {"6 in Reihe", "/gameplay/guitar_standard_inline/", "Wilshire_RH", "Wilshire_LH", nullptr},
    {"3+3", "/gameplay/guitar_standard/", "LP_RH", "LP_LH", "peg_bakelite"},
};

static_assert(sizeof kLayouts / sizeof kLayouts[0] == kLayoutCount, "kLayoutCount");

const HeadLayout* Rs7Asset_LayoutOf(const char* name, bool* leftHanded) {
    if (!name) {
        return nullptr;
    }

    const size_t len = std::strlen(name);
    if (len < 5 || len > 240 || _stricmp(name + len - 4, ".nif") != 0) {
        return nullptr;
    }

    std::string low(name, len);

    for (char& ch : low) {
        ch = char((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch == '\\' ? '/' : ch);
    }

    const size_t slash = low.find_last_of('/');
    const std::string base = low.substr(slash + 1, low.size() - slash - 1 - 4);
    if (base.empty()) {
        return nullptr;
    }

    const bool lh = base.size() >= 3 && base.compare(base.size() - 3, 3, "_lh") == 0;

    for (const HeadLayout& l : kLayouts) {
        const size_t flen = std::strlen(l.folder);
        const size_t at = low.find(l.folder);
        if (at == std::string::npos || slash != at + flen - 1) {
            continue;  // directly in that folder
        }

        if (leftHanded) {
            *leftHanded = lh;
        }
        return &l;
    }

    return nullptr;
}

bool Rs7Asset_IsRhCandidate(const char* name) {
    bool lh = false;

    return Rs7Asset_LayoutOf(name, &lh) && !lh;
}

bool Rs7Asset_IsLayoutPeg(const char* name) {
    bool lh = false;
    const HeadLayout* l = Rs7Asset_LayoutOf(name, &lh);
    if (!l || !l->peg) {
        return false;
    }

    std::string low(name);

    for (char& ch : low) {
        ch = char((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch == '\\' ? '/' : ch);
    }

    const size_t slash = low.find_last_of('/');

    return low.substr(slash + 1) == std::string(l->peg) + ".nif";
}

// The left-handed twin: the same folders, base name ending in "_lh" (epi_whilshire_cherry_lh.nif,
// gib_lespaul_lh.nif: the frames the game loads for a left-handed profile).
bool Rs7Asset_IsLhCandidate(const char* name) {
    bool lh = false;

    return Rs7Asset_LayoutOf(name, &lh) && lh;
}

namespace {
// "X:..." or a UNC path: the resolvers below keep it as it is.
bool IsAbsoluteWin(const std::wstring& v) {
    return (v.size() > 1 && v[1] == L':') || (v.size() > 1 && v[0] == L'\\' && v[1] == L'\\');
}
}  // namespace

std::wstring Rs7Asset_ResolveFile(const std::wstring& value, const std::wstring& dir) {
    std::wstring v = value;

    while (!v.empty() && (v.back() == L' ' || v.back() == L'\t')) {
        v.pop_back();
    }

    size_t k = 0;

    while (k < v.size() && (v[k] == L' ' || v[k] == L'\t')) {
        ++k;
    }

    v.erase(0, k);
    if (v.empty() || _wcsicmp(v.c_str(), L"none") == 0 || _wcsicmp(v.c_str(), L"off") == 0 || v == L"0") {
        return {};
    }
    if (IsAbsoluteWin(v) || dir.empty()) {
        return v;
    }

    std::wstring out = dir;

    if (out.back() != L'\\' && out.back() != L'/') {
        out += L'\\';
    }

    return out + v;
}

std::wstring Rs7Asset_ResolveDir(const std::wstring& value, const std::wstring& dllDir) {
    std::wstring v = value;

    while (!v.empty() && (v.back() == L' ' || v.back() == L'\t')) {
        v.pop_back();
    }

    size_t k = 0;

    while (k < v.size() && (v[k] == L' ' || v[k] == L'\t')) {
        ++k;
    }

    v.erase(0, k);
    if (v.empty()) {
        v = kDefaultDir;
    }
    if (IsAbsoluteWin(v) || dllDir.empty()) {
        return v;
    }

    std::wstring out = dllDir;

    if (out.back() != L'\\' && out.back() != L'/') {
        out += L'\\';
    }

    return out + v;
}

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
        bool dup = false;

        for (const std::string& x : out) {
            dup = dup || _stricmp(x.c_str(), n.c_str()) == 0;
        }

        if (!dup) {
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

std::string Rs7Asset_FolderOf(const char* name) {
    if (!name) {
        return {};
    }

    std::string s(name);
    const size_t k = s.find_last_of("/\\");

    return k == std::string::npos ? std::string() : s.substr(0, k + 1);
}

void Rs7Asset_Configure(const wchar_t* iniPath) {
    const int on = int(Rs7Config_ReadInt(L"assets", L"headstock7", 1, iniPath));
    // The built-in defaults are relative to the DLL's folder, which is the folder of rs7string.ini
    // (startup.cpp IniPath).
    std::wstring dllDir = iniPath ? iniPath : L"";
    const size_t sep = dllDir.find_last_of(L"\\/");

    dllDir = sep == std::wstring::npos ? std::wstring() : dllDir.substr(0, sep + 1);
    const wchar_t* truncated = nullptr;
    auto readSetting = [&](const wchar_t* key, const wchar_t* fallback, wchar_t* out, DWORD capacity) {
        if (GetPrivateProfileStringW(L"assets", key, fallback, out, capacity, iniPath) >= capacity - 1) {
            truncated = key;  // exact-fit is indistinguishable from truncation: fail closed
        }
    };
    wchar_t buf[MAX_PATH] = {};

    readSetting(L"dir", kDefaultDir, buf, MAX_PATH);
    g_dir = Rs7Asset_ResolveDir(buf, dllDir);
    wchar_t suf[64] = {};

    readSetting(L"suffix", kDefaultSuffix, suf, 64);
    g_suffix = suf;

    for (wchar_t ch : g_suffix) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'.') {
            g_suffix = kDefaultSuffix;
        }
    }

    wchar_t file[MAX_PATH] = {};

    readSetting(L"headstock7_file", L"?", file, MAX_PATH);
    const bool fileKey = !(file[0] == L'?' && file[1] == 0);

    g_file7 = Rs7Asset_ResolveFile(fileKey ? std::wstring(file) : std::wstring(kDefaultFile7), g_dir);
    wchar_t fileLh[MAX_PATH] = {};

    readSetting(L"headstock7_file_lh", L"?", fileLh, MAX_PATH);
    const bool fileKeyLh = !(fileLh[0] == L'?' && fileLh[1] == 0);

    g_file7Lh = Rs7Asset_ResolveFile(fileKeyLh ? std::wstring(fileLh) : std::wstring(kDefaultFile7Lh), g_dir);
    wchar_t wood[32] = {};

    readSetting(L"headstock7_wood", L"", wood, 32);

    if (truncated) {
        g_rs7_asset_on = 0;
        g_sbWanted = g_sbWantedLh = false;
        g_file7.clear();
        g_file7Lh.clear();
        Rs7Log("config: [assets] %ls truncated: headstock7 disabled; shorten the value", truncated);
        return;
    }

    g_wood = kDefaultWood;

    if (wood[0]) {
        wchar_t* end = nullptr;
        const double v = std::wcstod(wood, &end);
        const bool parsed = end != wood;

        while (*end && std::iswspace(*end)) {
            ++end;
        }

        if (parsed && !*end && std::isfinite(v) && v > 0.05 && v <= 1.0) {
            g_wood = float(v);
        }
    }

    g_rs7_asset_on = on ? 1 : 0;
    const bool exists = !g_file7.empty() && GetFileAttributesW(g_file7.c_str()) != INVALID_FILE_ATTRIBUTES;

    Rs7Log("config: [assets] headstock7_wood=%.2f%s (brightness of the 7-string model's wood colour texture)",
           double(g_wood), wood[0] ? "" : " (default)");
    Rs7Log("config: [assets] headstock7=%d headstock7_file=%ls%s dir=%ls suffix=%ls -> %s", on,
           g_file7.empty() ? L"(none)" : g_file7.c_str(),
           g_file7.empty() ? ""
           : exists        ? (fileKey ? "" : " (default)")
                           : " (MISSING: the edited-file rule only)",
           g_dir.c_str(), g_suffix.c_str(),
           on ? "7-string guitars get the 7-string headstock model (AL1 + N5): headstock7_file for the in-line "
                "right-hand frame, else <dir>\\<base><suffix>.nif"
              : "off (AL1 replays stock)");
    // Issue #28: no usable model file -> build one at the first guitar frame load (the game's VFS is up then).
    const std::wstring fileVal = fileKey ? Rs7Asset_ResolveFile(file, g_dir) : std::wstring();
    const bool fileOff = fileKey && fileVal.empty();  // headstock7_file=none/off/0: the owner opted out
    const bool sb = Rs7Config_ReadInt(L"assets", L"selfbuild", 1, iniPath) != 0;

    g_sbWanted = on && !exists && !fileOff && sb;

    if (g_sbWanted) {
        g_sbIni = iniPath ? iniPath : L"";
        g_sbDllDir = dllDir;
        Rs7Log("config: [assets] selfbuild=1 and no usable headstock7_file -> the 7-string model is built from the "
               "game's own "
               "assets at the first guitar load, into %ls",
               g_dir.c_str());
    } else if (sb) {
        Rs7Log("config: [assets] selfbuild=1 -> not needed (%s)", !on      ? "headstock7=0"
                                                                  : exists ? "headstock7_file exists"
                                                                           : "headstock7_file=none");
    }

    if (!exists) {
        g_file7.clear();
    }
    // Left-handed in-line frame (epi_whilshire_cherry_lh.nif): same rules, own key, own self-built file.
    const bool existsLh = !g_file7Lh.empty() && GetFileAttributesW(g_file7Lh.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool fileOffLh = fileKeyLh && g_file7Lh.empty();

    g_sbWantedLh = on && !existsLh && !fileOffLh && sb;

    if (g_sbWantedLh) {
        g_sbIni = iniPath ? iniPath : L"";
        g_sbDllDir = dllDir;
    }

    Rs7Log("config: [assets] headstock7_file_lh=%ls%s -> %s", g_file7Lh.empty() ? L"(none)" : g_file7Lh.c_str(),
           g_file7Lh.empty() ? ""
           : existsLh        ? (fileKeyLh ? "" : " (default)")
                             : " (missing)",
           !on            ? "off"
           : existsLh     ? "left-handed in-line frame gets this model"
           : g_sbWantedLh ? "selfbuild at the first left-handed guitar load"
                          : "left-handed frame stays stock");
    if (!existsLh) {
        g_file7Lh.clear();
    }
}

std::wstring Rs7Asset_PublishedPath(const std::wstring& path, volatile long* state) {
    // A wait budget expiring does not finish the builder. Acquire its completion publication
    // before touching std::wstring storage; otherwise use the edited-file/stock fallback.
    if (_InterlockedCompareExchange(state, 2, 2) != 2) {
        return {};
    }

    return path;
}

bool Rs7Asset_RunOnce(volatile long* state, bool (*build)(void*), void* context, unsigned waitMs) {
    if (_InterlockedCompareExchange(state, 1, 0) == 0) {
        bool ok = false;

        try {
            ok = build(context);
        } catch (...) {
            ok = false;
        }

        _InterlockedExchange(state, ok ? 2 : 3);
        return ok;
    }

    for (unsigned waited = 0; *state < 2 && waited < waitMs; waited += 50) {
        Sleep(50);
    }

    return _InterlockedCompareExchange(state, 2, 2) == 2;
}

namespace {
volatile long g_sbShared = 0;  // the right-handed build (shared rs7hs_*.dds + RH model): one owner for both jobs

bool BuildSharedRh(void*) {
    return Rs7Asset_SelfBuild(g_sbIni.c_str(), g_sbDllDir.c_str(), g_dir.c_str(), true);
}

// First guitar-frame nif load (loader thread): build the model if it is not there yet. Other loader threads
// wait (bounded) so every thread sees the final g_file7. Any failure leaves g_file7 empty = stock model.
void SelfBuildOnce(bool lh) {
    volatile long& state = lh ? g_sbStateLh : g_sbState;

    if (_InterlockedCompareExchange(&state, 1, 0) == 0) {
        const std::wstring file = g_dir + (lh ? kDefaultFile7Lh : kDefaultFile7);
        // The left-handed model references the shared rs7hs_*.dds: the right-handed build (cached when there)
        // puts them into the same folder first. Both jobs go through ONE owner of that build (review #14): the
        // second job waits for / reuses its result instead of writing the same intermediate files concurrently.
        bool ok = Rs7Asset_RunOnce(&g_sbShared, BuildSharedRh, nullptr, 30000);

        if (ok && lh) {
            ok = Rs7Asset_SelfBuild(g_sbIni.c_str(), g_sbDllDir.c_str(), g_dir.c_str(), true, true);
        }

        if (ok && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            (lh ? g_file7Lh : g_file7) = file;
            Rs7Log("assetload: self-built %s7-string model in use: %ls", lh ? "left-handed " : "", file.c_str());
        } else {
            Rs7Log(
                "assetload: %sselfbuild gave no model - the stock 6-peg headstock stays (see the hsbuild line above)",
                lh ? "left-handed " : "");
        }

        _InterlockedExchange(&state, 2);
        return;
    }

    for (int i = 0; i < 600 && state != 2; ++i) {
        Sleep(50);  // 30 s at most
    }
}
}  // namespace

namespace {
// Whether the stock model just loaded is a guitar headstock frame (the node names the game binds:
// Locator_Peg_1 and string_size_6 present, string_size_7 absent = not a bass frame).
bool IsGuitarFrame(void* res) {
    uint8_t* top = TopObject(res);
    if (!top || !rs7s7d::IsKindOf(top, "NiNode")) {
        return false;
    }

    return FindByName(top, "Locator_Peg_1") && FindByName(top, "string_size_6") && !FindByName(top, "string_size_7");
}
}  // namespace

namespace {
// 3+3 peg (kLayouts comment): the in-line peg and its wilshire_hardware.dds from the in-line package (the game's
// own reader; that package is not loaded with the 3+3 option), the texture registered in the peg's folder, the
// peg merged as rs7_alt (culled). Any failure: the stock peg only (one log line).
void MergeInlinePeg(const char* name, void* res, const void* stockBuf, uint32_t stockSize) {
    std::vector<uint8_t> peg, hw;

    if (!Rs7AssetDump_ReadInlinePeg(peg, hw)) {
        Rs7Log("assetload: %s: the in-line peg (epi_whilshire_peg.nif + wilshire_hardware.dds) could not be read from "
               "dependency_guitarstandardinlinedrh.psarc - 7-string guitars keep this option's pegs",
               name);
        return;
    }

    const std::vector<std::string> stock =
        Rs7Asset_DdsNames(static_cast<const uint8_t*>(stockBuf), stockBuf ? stockSize : 0);
    const std::string key = Rs7Asset_FolderOf(name) + "wilshire_hardware.dds";
    void* tex =
        INameIn("wilshire_hardware.dds", stock) ? nullptr : CallDdsHandler(key.c_str(), hw.data(), uint32_t(hw.size()));

    Rs7Log("assetload: %s: in-line peg read (%u bytes), wilshire_hardware.dds (%u bytes) registered as \"%s\": "
           "resource %p",
           name, unsigned(peg.size()), unsigned(hw.size()), key.c_str(), tex);
    Merge(name, res, L"<game> guitar_standard_inline/epi_whilshire_peg.nif", peg);
}
}  // namespace

extern "C" void* __cdecl Rs7_NifHandler(const char* name, const void* buf, uint32_t size, void* ctx) {
    void* res = Rs7_NifHandlerStock(name, buf, size, ctx);  // exactly the stock call, faults included
    if (!res || !g_rs7_asset_on) {
        return res;
    }

    try {
        if (Rs7Asset_IsLayoutPeg(name)) {
            MergeInlinePeg(name, res, buf, size);
            return res;
        }

        const bool rh = Rs7Asset_IsRhCandidate(name), lh = Rs7Asset_IsLhCandidate(name);

        if (rh && g_sbWanted && g_sbState != 2 && IsGuitarFrame(res)) {
            SelfBuildOnce(false);
        }
        if (lh && g_sbWantedLh && g_sbStateLh != 2 && IsGuitarFrame(res)) {
            SelfBuildOnce(true);
        }
        const bool selfbuilding = lh ? g_sbWantedLh : g_sbWanted;
        const std::wstring own7 = selfbuilding
                                      ? Rs7Asset_PublishedPath(lh ? g_file7Lh : g_file7, lh ? &g_sbStateLh : &g_sbState)
                                      : (lh ? g_file7Lh : g_file7);
        std::wstring path;
        bool own = false;

        if ((rh || lh) && !own7.empty() && IsGuitarFrame(res)) {
            path = own7;
            own = true;
        } else {
            path = Rs7Asset_EditedPath(name, g_dir, g_suffix);
            if (path.empty()) {
                return res;
            }

            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (Upto(&g_logMissing, 12)) {
                    Rs7Log("assetload: %s: no edited model (%ls) - stock", name, path.c_str());
                }
                return res;
            }
        }

        if (Upto(&g_logSeen, 16)) {
            Rs7Log("assetload: %s (%u bytes) loaded by the nif handler; %s %ls", name, unsigned(size),
                   own ? "7-string model (headstock7_file)" : "edited model", path.c_str());
        }

        if (own) {
            bool isLh = false;
            const HeadLayout* lay = Rs7Asset_LayoutOf(name, &isLh);

            if (lay && Upto(&g_logLayout, 4)) {
                Rs7Log("assetload: headstock layout \"%s\" (%s frame, stock part %s)", lay->option,
                       isLh ? "left-handed" : "right-handed", isLh ? lay->stockLh : lay->stockRh);
            }
        }

        std::vector<uint8_t> bytes;

        if (!ReadFileBytes(path, bytes)) {
            if (Upto(&g_logFail, 8)) {
                Rs7Log("assetload: %s: could not read %ls - stock model only", name, path.c_str());
            }
            return res;
        }

        if (own) {
            const int n = RegisterTextures(name, path, bytes, buf, size);

            Rs7Log("assetload: %s: %d texture(s) of the 7-string model registered in %s", name, n,
                   Rs7Asset_FolderOf(name).c_str());
        }

        Merge(name, res, path, bytes);
        if (own && Upto(&g_logTexCheck, 2)) {
            LogTextureList("after the merge");
        }
    } catch (...) {
    }

    return res;
}

int Rs7Asset_LiveHeads() {
    int n = 0;

    for (const Head& h : g_heads) {
        n += h.guitar != nullptr;
    }

    return n;
}

// The guitar is being released (Rs7_S7Free from the native Rnd_GuitarStrings_Free copy, called from the guitar's
// release 0x592770 with guitar+0Ch still its strings): its state slot goes back to the table. Without
// this every guitar object the game ever builds keeps a slot (also the stock-headstock ones: Slot() runs
// before the `!want` early-out), and the 9th distinct address silently ends the edited headstock.
void Rs7Asset_ForgetStrings(const uint8_t* gs) {
    if (!gs) {
        return;
    }

    for (Head& h : g_heads) {
        if (!h.guitar) {
            continue;
        }

        uint8_t* strings = nullptr;
        if (!rs7s7d::SafeCopy(&strings, h.guitar + 0x0C, sizeof strings) || strings != gs) {
            continue;
        }

        h = Head{};
        Rs7Alloc_Dec(kAkHeadSlots);
    }
}

void Rs7Asset_ForgetGuitar(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    for (Head& h : g_heads) {
        if (h.guitar != guitar) {
            continue;
        }

        h = Head{};
        Rs7Alloc_Dec(kAkHeadSlots);
    }
}

void Rs7Asset_HeadFrame(uint8_t* guitar, bool want) {
    if (!g_rs7_asset_on || !guitar) {
        return;
    }

    uint8_t* head = Rd<uint8_t*>(guitar, 0x18);                  // Rnd_Guitar: Rnd_Headstock*
    uint8_t* frame = head ? Rd<uint8_t*>(head, 0x04) : nullptr;  // frame entity
    Head* h = Slot(guitar);
    if (!h) {
        return;
    }

    if (!want && !h->applied) {  // every stock frame: two compares (three after a 3+3 peg swap)
        if (g_pegSwapEver) {
            StockPegs(*h);
        }
        return;
    }

    uint8_t* root = frame ? rs7s7d::EntityRoot(frame) : nullptr;

    if (root != h->root) {  // new frame instance (or none): forget the old pointers
        h->root = root;
        h->alt = root ? FindAlt(root) : nullptr;
        h->nsibs = 0;
        h->applied = false;
        h->bound = false;
        h->pointed = false;
        h->locSwapped = false;
        h->altLoc7 = nullptr;
        h->texChecked = false;
        std::memset(h->params7, 0, sizeof h->params7);  // the old frame's objects (refs leaked, never freed)
        h->frames = 0;
        if (want && !h->alt && root && Upto(&g_logNoAlt, 4)) {
            Rs7Log("assetload: guitar %p (frame entity %p, root %p) has no %s node: no edited model merged for it",
                   guitar, frame, root, kAltName);
        }
    }

    if (!h->alt) {
        return;
    }

    if (want) {
        Show(*h);

        if (++h->frames == 1 && Upto(&g_logFrame, 8)) {
            char sib[48] = {};

            if (h->nsibs) {
                rs7s7d::NameOfNode(h->sibs[0], sib, sizeof sib);
            }
            uint8_t* info = Rd<uint8_t*>(guitar, 0x24);

            Rs7Log("assetload: guitar %p shows the edited headstock (%s %p in frame root %p; %d stock part(s) culled, "
                   "first \"%s\"; InstrumentInfo inverted %u lefty %u in-line %u)",
                   guitar, kAltName, h->alt, root, h->nsibs, sib, unsigned(Rd<uint8_t>(info, 8)),
                   unsigned(Rd<uint8_t>(info, 9)), unsigned(Rd<uint8_t>(info, 0x0A)));
        }
    } else {
        Restore(*h);
        h->frames = 0;
        if (Upto(&g_logFrame, 8)) {
            Rs7Log("assetload: guitar %p back to the stock headstock", guitar);
        }
    }
}

volatile long g_logLetters = 0;

extern "C" int __cdecl Rs7Asset_LetterInline(const uint8_t* info) {
    const int own = info ? Rd<uint8_t>(info, 0x0A) : 0;
    if (own || !info || !g_rs7_asset_on) {
        return own;
    }

    int r = 0;

    for (const Head& h : g_heads) {
        if (h.guitar && h.applied && h.onNonInline && Rd<const uint8_t*>(h.guitar, 0x24) == info) {
            r = 1;
        }
    }

    static const uint8_t* lastInfo = nullptr;
    static int lastR = -1;

    if ((info != lastInfo || r != lastR) && Upto(&g_logLetters, 8)) {
        Rs7Log("assetload: LT1 tuner letters of InstrumentInfo %p (in-line byte 0): %s", info,
               r ? "the guitar shows the 7-string in-line headstock - in-line letter rule" : "stock 3+3 rule");
    }
    lastInfo = info;
    lastR = r;

    return r;
}

uint8_t* Rs7Asset_Seg7Colour(uint8_t* guitar) {
    if (!g_rs7_asset_on || !guitar) {
        return nullptr;
    }

    for (Head& h : g_heads) {
        if (h.guitar == guitar) {
            return (h.applied && h.seg7) ? reinterpret_cast<uint8_t*>(h.params7[3]) : nullptr;
        }
    }

    return nullptr;
}
