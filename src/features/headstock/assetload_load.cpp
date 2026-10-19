#include "features/allocstats/allocstats.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_config.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/assetload_tex.h"
#include "features/headstock/headstockread.h"
#include "features/headstock/hsbuild_io.h"
#include "platform/log.h"
#include <windows.h>
#include <string>
#include <vector>

// Threads: Rs7_NifHandler runs on the resource manager's loader threads (Res_WorkerLoopA/B), on a
// master scene graph nothing else has seen yet. Rs7Asset_HeadFrame runs on the game thread (N5).
// The stock path (headstock7=0) never reaches these files: the AL1 stub replays the entry itself.

using namespace rs7asset;

namespace {
volatile long g_logSeen = 0, g_logMerged = 0, g_logMissing = 0, g_logFail = 0, g_logLayout = 0, g_logTexCheck = 0;

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
    Rename(alt, kAltName);
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
}  // namespace

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

// Whether the stock model just loaded is a guitar headstock frame (the node names the game binds:
// Locator_Peg_1 and string_size_6 present, string_size_7 absent = not a bass frame).
bool IsGuitarFrame(void* res) {
    uint8_t* top = TopObject(res);
    if (!top || !rs7s7d::IsKindOf(top, "NiNode")) {
        return false;
    }

    return FindByName(top, "Locator_Peg_1") && FindByName(top, "string_size_6") && !FindByName(top, "string_size_7");
}

// 3+3 peg (kLayouts comment): the in-line peg and its wilshire_hardware.dds from the in-line package (the game's
// own reader; that package is not loaded with the 3+3 option), the texture registered in the peg's folder, the
// peg merged as rs7_alt (culled). Any failure: the stock peg only (one log line).
void MergeInlinePeg(const char* name, void* res, const void* stockBuf, uint32_t stockSize) {
    std::vector<uint8_t> peg, hw;

    if (!Rs7HeadstockRead_InlinePeg(peg, hw)) {
        Rs7Log("assetload: %s: the in-line peg (epi_whilshire_peg.nif + wilshire_hardware.dds) could not be read from "
               "dependency_guitarstandardinlinedrh.psarc - 7-string guitars keep this option's pegs",
               name);
        return;
    }

    const std::vector<std::string> stock = StockDdsNames(stockBuf, stockSize);
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
