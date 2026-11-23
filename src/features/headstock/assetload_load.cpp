#include "features/allocstats/allocstats.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_config.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/assetload_tex.h"
#include "features/headstock/headstockread.h"
#include "features/headstock/hsbuild_io.h"
#include "features/headstock/state.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <string>
#include <vector>

// Threads: Rsxr_NifHandler runs on the resource manager's loader threads (Res_WorkerLoopA/B), on a
// master scene graph nothing else has seen yet. RsxrAsset_HeadFrame runs on the game thread (N5).
// The stock path (headstock7=0) never reaches these files: the AL1 stub replays the entry itself.

using namespace rsxrasset;
using rsxrhead::g_head;

namespace {
bool AttachChild(uint8_t* parent, uint8_t* child) {  // NiNode vtbl+0A0h, thiscall(child, bFirstAvail)
    __try {
        void* const* vt = *reinterpret_cast<void* const* const*>(parent);
        using Fn = void(__fastcall*)(uint8_t*, void*, uint8_t*, uint32_t);

        reinterpret_cast<Fn>(vt[kNodeAttach / 4])(parent, nullptr, child, 0);
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

void* CallStock(const char* name, const void* buf, uint32_t size, void* ctx) {
    __try {
        return Rsxr_NifHandlerStock(name, buf, size, ctx);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// Loads the edited file with the stock handler (same resource name) and hangs its top object,
// renamed and culled, under the stock top object. The edited resource is kept (never released):
// its NiStream holds the only other references and the master lives as long as the game.
void Merge(const char* name, void* res, const std::wstring& path, const std::vector<uint8_t>& bytes,
           const char* altName = kAltName) {
    uint8_t* top = TopObject(res);

    if (!top || !rsxrs7d::IsKindOf(top, "NiNode")) {
        if (RsxrLog_Upto(&g_head.logFail, 8)) {
            RsxrLog("assetload: %s: stock top object %p is not an NiNode - not merged", name, top);
        }
        return;
    }

    void* res2 = CallStock(name, bytes.data(), uint32_t(bytes.size()), nullptr);
    uint8_t* alt = res2 ? TopObject(res2) : nullptr;

    if (!alt || !rsxrs7d::IsKindOf(alt, "NiNode") || Rsxr_SafeRead<uint8_t*>(alt, kParent)) {
        if (RsxrLog_Upto(&g_head.logFail, 8)) {
            RsxrLog("assetload: %s: %ls did not load as a scene (resource %p, top %p) - stock model only", name,
                    path.c_str(), res2, alt);
        }
        return;
    }

    char oldName[48] = {};

    rsxrs7d::NameOfNode(alt, oldName, sizeof oldName);
    Rename(alt, altName);
    SetCulled(alt, true);  // every clone starts with the stock model shown
    const unsigned before = Rsxr_SafeRead<uint16_t>(top, kChildSlots);

    if (!AttachChild(top, alt)) {
        if (RsxrLog_Upto(&g_head.logFail, 8)) {
            RsxrLog("assetload: %s: AttachChild faulted - stock model only", name);
        }
        return;
    }

    RsxrAlloc_Inc(kAkAssetMerges);
    char topName[48] = {}, newName[48] = {};

    rsxrs7d::NameOfNode(top, topName, sizeof topName);
    rsxrs7d::NameOfNode(alt, newName, sizeof newName);

    if (RsxrLog_Upto(&g_head.logMerged, 16)) {
        RsxrLog("assetload: %s: merged %ls (%u bytes): its top \"%s\" %p -> \"%s\", culled, attached under the stock "
                "top \"%s\" %p (child slots %u -> %u, parent now %p)",
                name, path.c_str(), unsigned(bytes.size()), oldName, alt, newName, topName, top, before,
                unsigned(Rsxr_SafeRead<uint16_t>(top, kChildSlots)), Rsxr_SafeRead<void*>(alt, kParent));
    }
}
}  // namespace

std::wstring RsxrAsset_PublishedPath(const std::wstring& path, volatile long* state) {
    // A wait budget expiring does not finish the builder. Acquire its completion publication
    // before touching std::wstring storage; otherwise use the edited-file/stock fallback.
    if (_InterlockedCompareExchange(state, 2, 2) != 2) {
        return {};
    }

    return path;
}

bool RsxrAsset_RunOnce(volatile long* state, bool (*build)(void*), void* context, unsigned waitMs) {
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
bool BuildSharedRh(void*) {
    return RsxrAsset_SelfBuild(g_config.sbDllDir.c_str(), g_config.dir.c_str(), RsxrHsKit::Guitar,
                               g_config.sbProtected);
}

bool BuildBass(void* lh) {
    return RsxrAsset_SelfBuild(g_config.sbDllDir.c_str(), g_config.dir.c_str(),
                               lh ? RsxrHsKit::BassLh : RsxrHsKit::Bass, g_config.sbProtected);
}

// First bass-frame nif load without bass models (loader thread): the guitar kit (the shared textures), the bass kit
// (its textures and the right-handed models), then for a left-handed frame the mirrored models. Once each.
bool BassSelfBuild(bool lh) {
    if (!RsxrAsset_RunOnce(&g_head.sbShared, BuildSharedRh, nullptr, 30000) ||
        !RsxrAsset_RunOnce(&g_head.sbBassRh, BuildBass, nullptr, 30000)) {
        return false;
    }

    if (lh) {
        return RsxrAsset_RunOnce(&g_head.sbBassLh, BuildBass, &g_head.bassLhTag, 30000);
    }

    return true;
}

// First guitar-frame nif load (loader thread): build the model if it is not there yet. Other loader threads
// wait (bounded) so every thread sees the final g_config.file7. Any failure leaves g_config.file7 empty = stock model.
bool BuildGuitar(void* hand) {
    const bool lh = hand != nullptr;
    std::wstring& published = lh ? g_config.file7Lh : g_config.file7;

    published.clear();
    ModelFiles model = ModelFilesFor(true);
    std::wstring file = RsxrAsset_ResolveFile(lh ? model.fileLh : model.file, g_config.dir);
    // The left-handed model references the shared rsxrhs_*.dds: the right-handed build (cached when there)
    // puts them into the same folder first. Both jobs go through ONE owner of that build: the
    // second job waits for / reuses its result instead of writing the same intermediate files concurrently.
    bool ok = RsxrAsset_RunOnce(&g_head.sbShared, BuildSharedRh, nullptr, 30000);

    if (ok && lh) {
        ok = RsxrAsset_SelfBuild(g_config.sbDllDir.c_str(), g_config.dir.c_str(), RsxrHsKit::GuitarLh,
                                 g_config.sbProtected);
    }

    // Eight strings without the 8-peg model (an older headstock package): the 7-peg one, not the stock headstock.
    if (ok && !FilePresent(file)) {
        const ModelFiles seven = ModelFilesFor(false);
        const std::wstring file7 = RsxrAsset_ResolveFile(lh ? seven.fileLh : seven.file, g_config.dir);

        if (FilePresent(file7)) {
            RsxrLog("assetload: %sno 8-peg model (%ls) - the 7-peg model instead", lh ? "left-handed: " : "",
                    file.c_str());
            model = seven;
            _InterlockedExchange(&g_head.modelPegs[lh ? 1 : 0], long(seven.pegs));
            file = file7;
        }
    }

    if (ok && FilePresent(file)) {
        published = file;
        RsxrLog("assetload: self-built %s%u-peg model in use: %ls", lh ? "left-handed " : "", model.pegs, file.c_str());
        return true;
    } else {
        RsxrLog("assetload: %sselfbuild gave no model - the stock 6-peg headstock stays (see the hsbuild line above)",
                lh ? "left-handed " : "");
    }

    return false;
}

void SelfBuildOnce(bool lh) {
    RsxrAsset_RunOnce(lh ? &g_head.sbStateLh : &g_head.sbState, BuildGuitar, lh ? &g_head.guitarLhTag : nullptr, 30000);
}

// Whether the stock model just loaded is a guitar headstock frame (the node names the game binds:
// Locator_Peg_1 and string_size_6 present, string_size_7 absent = not a bass frame).
bool IsGuitarFrame(void* res) {
    uint8_t* top = TopObject(res);
    if (!top || !rsxrs7d::IsKindOf(top, "NiNode")) {
        return false;
    }

    return FindByName(top, "Locator_Peg_1") && FindByName(top, "string_size_6") && !FindByName(top, "string_size_7");
}

// Whether it is a stock bass headstock frame (Locator_Peg_1 and the bass string_size_7..10).
bool IsBassFrame(void* res) {
    uint8_t* top = TopObject(res);

    return top && rsxrs7d::IsKindOf(top, "NiNode") && FindByName(top, "Locator_Peg_1") &&
           FindByName(top, "string_size_7");
}

// Bass: the frame is loaded once and cached, before any song sets the string count, so it gets both models: the
// 6-string one as rsxr_alt (its Locator_Peg_6 lets the stock binder take peg slot 5) and the 5-string one as rsxr_alt5;
// the head shows the one the bass is built with (assetload_head.cpp). Returns the rsxr_alt file ("" = none), `alt5`
// the rsxr_alt5 file ("" = none).
std::wstring BassModels(const char* name, void* res, std::wstring& alt5) {
    bool lh = false;
    const std::wstring six = RsxrAsset_BassPath(name, g_config.dir, 6),
                       five = RsxrAsset_BassPath(name, g_config.dir, 5, &lh);

    // No bass model yet: the self-build writes the shipped ones (rsxr_headstock_bass*.bin).
    // An outdated one (an update shipped a new package): rebuilt the same way, once.
    const bool stale = lh ? g_head.sbBassStaleLh : g_head.sbBassStale;

    if (g_head.sbBass && !five.empty() && ((!FilePresent(five) || !FilePresent(six)) || stale) && IsBassFrame(res)) {
        if (!BassSelfBuild(lh)) {
            return {};
        }
    }

    alt5 = FilePresent(six) && FilePresent(five) ? five : std::wstring();

    return FilePresent(six) ? six : FilePresent(five) ? five : std::wstring();
}

// Our own model (`own`: textures registered first) or an edited one merged into the frame; a bass frame also gets the
// 5-string model and a guitar frame with the 8-peg model the 7-peg one (`second`, rsxr_alt5), with their own textures
// (the shared ones registered once).
void MergeModels(const char* name, void* res, const void* buf, uint32_t size, bool own, const std::wstring& path,
                 std::vector<uint8_t>& bytes, const std::wstring& second) {
    std::vector<std::string> registered;

    if (own) {
        const int n = RegisterTextures(name, path, bytes, buf, size, &registered);

        RsxrLog("assetload: %s: %d texture(s) of the own model registered in %s", name, n,
                RsxrAsset_FolderOf(name).c_str());
    }

    Merge(name, res, path, bytes);

    if (own && !second.empty() && ReadFileBytes(second, bytes)) {
        RegisterTextures(name, second, bytes, buf, size, &registered);
        Merge(name, res, second, bytes, kAltName5);
    }
}

// Eight strings: the 7-peg model file next to the 8-peg one, "" without one (or with eight off). The self-build writes
// both in-line models.
std::wstring Seven(bool lh) {
    const std::wstring& f = lh ? g_config.file7sLh : g_config.file7s;
    const bool primary =
        f == (lh ? g_config.file7Lh : g_config.file7);  // the 7-peg fallback is the frame's model already

    return !primary && FilePresent(f) ? f : std::wstring();
}

// 3+3 peg (kLayouts comment): the in-line peg and its wilshire_hardware.dds from the in-line package (the game's
// own reader; that package is not loaded with the 3+3 option), the texture registered in the peg's folder, the
// peg merged as rsxr_alt (culled). Any failure: the stock peg only (one log line).
void MergeInlinePeg(const char* name, void* res, const void* stockBuf, uint32_t stockSize) {
    std::vector<uint8_t> peg, hw;

    if (!RsxrHeadstockRead_InlinePeg(peg, hw)) {
        RsxrLog("assetload: %s: the in-line peg (epi_whilshire_peg.nif + wilshire_hardware.dds) could not be read from "
                "dependency_guitarstandardinlinedrh.psarc - 7-string guitars keep this option's pegs",
                name);
        return;
    }

    const std::vector<std::string> stock = StockDdsNames(stockBuf, stockSize);
    const std::string key = RsxrAsset_FolderOf(name) + "wilshire_hardware.dds";
    void* tex =
        INameIn("wilshire_hardware.dds", stock) ? nullptr : CallDdsHandler(key.c_str(), hw.data(), uint32_t(hw.size()));

    RsxrLog("assetload: %s: in-line peg read (%u bytes), wilshire_hardware.dds (%u bytes) registered as \"%s\": "
            "resource %p",
            name, unsigned(peg.size()), unsigned(hw.size()), key.c_str(), tex);
    Merge(name, res, L"<game> guitar_standard_inline/epi_whilshire_peg.nif", peg);
}
}  // namespace

extern "C" void* __cdecl Rsxr_NifHandler(const char* name, const void* buf, uint32_t size, void* ctx) {
    void* res = Rsxr_NifHandlerStock(name, buf, size, ctx);  // exactly the stock call, faults included
    if (!res || !g_rsxr_asset_on) {
        return res;
    }

    try {
        if (RsxrAsset_IsLayoutPeg(name)) {
            MergeInlinePeg(name, res, buf, size);
            return res;
        }

        const bool rh = RsxrAsset_IsRhCandidate(name), lh = RsxrAsset_IsLhCandidate(name);

        if (rh && g_head.sbWanted && g_head.sbState < 2 && IsGuitarFrame(res)) {
            SelfBuildOnce(false);
        }
        if (lh && g_head.sbWantedLh && g_head.sbStateLh < 2 && IsGuitarFrame(res)) {
            SelfBuildOnce(true);
        }
        const bool selfbuilding = lh ? g_head.sbWantedLh : g_head.sbWanted;
        const std::wstring own7 = selfbuilding ? RsxrAsset_PublishedPath(lh ? g_config.file7Lh : g_config.file7,
                                                                         lh ? &g_head.sbStateLh : &g_head.sbState)
                                               : (lh ? g_config.file7Lh : g_config.file7);
        std::wstring path;
        bool own = false;

        std::wstring bass5;
        const std::wstring bass = BassModels(name, res, bass5);

        if ((rh || lh) && !own7.empty() && IsGuitarFrame(res)) {
            path = own7;
            own = true;
        } else if (!bass.empty() && IsBassFrame(res)) {
            path = bass;
            own = true;
        } else {
            path = RsxrAsset_EditedPath(name, g_config.dir, g_config.suffix);
            if (path.empty()) {
                return res;
            }

            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (RsxrLog_Upto(&g_head.logMissing, 12)) {
                    RsxrLog("assetload: %s: no edited model (%ls) - stock", name, path.c_str());
                }
                return res;
            }
        }

        if (RsxrLog_Upto(&g_head.logSeen, 16)) {
            RsxrLog("assetload: %s (%u bytes) loaded by the nif handler; %s %ls", name, unsigned(size),
                    !own           ? "edited model"
                    : bass.empty() ? "7-string model (headstock7_file)"
                                   : "extended-bass model",
                    path.c_str());
        }

        if (own) {
            bool isLh = false;
            const HeadLayout* lay = RsxrAsset_LayoutOf(name, &isLh);

            if (lay && RsxrLog_Upto(&g_head.logLayout, 4)) {
                RsxrLog("assetload: headstock layout \"%s\" (%s frame, stock part %s)", lay->option,
                        isLh ? "left-handed" : "right-handed", isLh ? lay->stockLh : lay->stockRh);
            }
        }

        std::vector<uint8_t> bytes;

        if (!ReadFileBytes(path, bytes)) {
            if (RsxrLog_Upto(&g_head.logFail, 8)) {
                RsxrLog("assetload: %s: could not read %ls - stock model only", name, path.c_str());
            }
            return res;
        }

        MergeModels(name, res, buf, size, own, path, bytes, path == bass ? bass5 : path == own7 ? Seven(lh) : L"");
    } catch (...) {
    }

    return res;
}
