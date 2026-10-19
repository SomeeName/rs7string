#pragma once
#include "features/headstock/headstock.h"
#include <cstdint>
#include <string>
#include <vector>

// The 7-string headstock model - PROTOTYPE (docs/ARCHITECTURE.md, issue #17). The part of the headstock feature
// that headstock.h does not export (g_rs7_asset_on, the per-frame and release entry points are there).
//
// The headstock frame of a guitar is one entity whose scene graph is a clone of the NIF the
// resource manager loaded (e.g. data/assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif
// from base\dependency_guitarstandardinlinedrh.psarc). The guitar is loaded once at boot and kept
// across songs, so a per-song file swap would never be seen. Instead both models live in the one
// frame entity:
//  - AL1 (entry of the nif memory handler Res_NifMemHandler 0x4A8560, cdecl (name, buf, size, ctx)
//    -> resource; +0Ch = the NiStream): after the stock load, the 7-string model for that guitar
//    model is loaded with the same handler and the same name (its texture names resolve in the
//    stock model's folder), its top object ("Scene Root") renamed rs7_alt, culled (APP_CULLED) and
//    attached as the LAST child of the stock top object (NiNode::AttachChild, vtbl+0A0h). Every
//    clone the game makes from that master (the frame entity) then carries both models; the stock
//    part comes first, so the stock name lookups (string_size_%d, Locator_Peg_%d) still bind the
//    stock nodes. Which file:
//      * [assets] headstock7_file (default: rs7_inline7.nif in dir, i.e. rs7string\headstock next to
//        the DLL; our own brandless "Facet" model, tools/rs7headstock) for the right-hand guitar frame of either
//        headstock option (HeadLayout: "6 in Reihe" = guitar_standard_inline, "3+3" = guitar_standard; not
//        *_lh, its tree has Locator_Peg_1 and string_size_6 but no string_size_7). Its textures (every .dds the model
//        names that the stock model does not name and that exists next to the model file) are registered first through
//        the game's own dds handler (X_DDSH 0x4A81D0, the nidds stream handler) under "<stock folder>/<name>", the key
//        the stock textures of that folder get. The handler reads through the game's NiFile adapter (X_NFCTOR /
//        X_NFDTOR) over an in-memory IFile of ours (the resource manager's IFile interface: Read, GetSize, Tell, Seek,
//        Size64, IsGood).
//      * otherwise (or headstock7_file=none) the edited-file rule: <dir>\<base><suffix>.nif.
//  - N5 (per frame, Rs7Asset_HeadFrame): on a guitar that shows the 7th string (the same
//    visibility as the 7th string line: S4 mark / 7-string tuner session) rs7_alt is un-culled and
//    its stock siblings are culled; otherwise the reverse. 6-string guitars and bass never change.
//    While shown: the six headstock string segment param objects (Rnd_Headstock +20h + s*4, filled
//    by Rnd_SetStringShaderParams 0x5998F0 = X_STRPARAMS) point at rs7_alt's string_size_<6-s>
//    extra data (values carried over); string_size_7 (string 6, the low B) gets its own params and
//    every frame the 7th string's colour, exactly as Rnd_Headstock_UpdateSegmentColours 0x58E4F0
//    copies string s's colour (+38h handle) onto segment s; the PegSet slots' locators (+8) point
//    at rs7_alt's Locator_Peg_<s+1> (pegs placed every shown frame as 0x59C630 does); Locator_Peg_7
//    (only in rs7_alt) is bound to the native PegSet's slot 6 by the stock binder 0x59C980 (pegset.h).
//    All undone when it is hidden again.
// [assets] headstock7=0: AL1 replays stock, nothing is read.

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_fn_fixstr;  // X_FIXSTR 0x91CBF0 NiFixedString create, cdecl(const char*)
// X_STRPARAMS 0x5998F0 Rnd_SetStringShaderParams: EAX = param object, cdecl (info, slot, mesh)
__declspec(dllexport) extern uintptr_t g_rs7_fn_strparams;
// X_DDSH 0x4A81D0 the nidds stream handler, cdecl (name, NiBinaryStream*, ctx) -> resource (+0Ch texture)
__declspec(dllexport) extern uintptr_t g_rs7_fn_dds;
// X_NFCTOR 0x49FF90 Res_NiFileAdapter_Ctor (ESI = 0x1C adapter, EDI = IFile*) and X_NFDTOR 0x4A0030 its
// destructor (ECX = adapter): the NiFile the dds handler reads through, as Res_ParseFileJob_Load 0x4A7BC4 builds it
__declspec(dllexport) extern uintptr_t g_rs7_fn_nfctor;
__declspec(dllexport) extern uintptr_t g_rs7_fn_nfdtor;
// AL1 (headstock7=1): replaces the stock handler call (same signature, cdecl).
__declspec(dllexport) void* __cdecl Rs7_NifHandler(const char* name, const void* buf, uint32_t size, void* ctx);
// Calls the stock handler (the replayed entry bytes of AL1 + jump back), cdecl.
__declspec(dllexport) void* __cdecl Rs7_NifHandlerStock(const char* name, const void* buf, uint32_t size, void* ctx);
}

namespace rs7asset {
constexpr uint32_t kResStream = 0x0C;        // nif resource (vtable 0x11A3560): +0Ch NiStream*
constexpr uint32_t kResTexture = 0x0C;       // dds resource (0x4AA170): +0Ch the NiSourceTexture
constexpr uint32_t kStreamTop = 0x15C;       // NiStream: top objects (NiTLargeObjectArray +158h: +4 base)
constexpr uint32_t kStreamTopCount = 0x164;  // NiStream: top objects, +0Ch count (0x9ED070 / 0x9EFBE0)
constexpr uint32_t kNodeAttach = 0xA0;       // NiNode vtbl+0A0h AttachChild(child, bFirstAvail) ret 8 (0x9DFD20)
constexpr uint32_t kParent = 0x24;           // NiAVObject parent (0x9E22F0)
constexpr uint32_t kNiFileSize = 0x1C;       // Res_NiFileAdapter (vtable 0x11A3220), +18h IFile*
constexpr const char* kAltName = "rs7_alt";
constexpr uint32_t kPGH1Skip = 0x54338A;  // PGH1: the pass's own next-mesh path (mov eax,[esi+10h])
// Built-in defaults. dir is relative to the DLL's folder (the game folder): rs7string\headstock next to it.
constexpr const wchar_t* kDefaultDir = L"rs7string\\headstock\\";
constexpr const wchar_t* kDefaultSuffix = L"_7inline";
// Our own brandless in-line 7-string headstock (tools/rs7headstock, MIT; the .nif is built from the
// user's own stock model and is not shipped). A name relative to dir: kDefaultDir + rs7_inline7.nif.
constexpr const wchar_t* kDefaultFile7 = L"rs7_inline7.nif";
constexpr const wchar_t* kDefaultFile7Lh = L"rs7_inline7_lh.nif";  // [assets] headstock7_file_lh default
constexpr float kDefaultWood = 0.6f;  // [assets] headstock7_wood default (owner 2026-10-13: the wood was too bright)
constexpr int kExtraStrings = 2;      // string_size_7/8, Locator_Peg_7/8 (8-string ready; 7 used)

// Noteway draw layer (issue #23). GRNoteway3DRenderView_Render 0x534B30 draws its list sorted by
// layer * 100000 + camera depth, highest first; the layer is the u16 at +20h of the object's NiProperty
// of type 0xC (vtable 0x11A79B0, ctor 0x530F70, setter 0x7ADE80), 1 without one. Stock
// Rnd_Headstock_LoadStringMeshes 0x58E090 gives string_size_1..6 layer 11 (call 0x7ADE80 at 0x58E2FC), like
// the neck strings, so they draw before the fret bars (layer 10) and the nut covers their ends; it
// never visits string_size_7, which kept the nif's 1 and drew after the bars, over the nut.
constexpr uint32_t kPropList = 0xB8;    // NiAVObject property list: nodes {next, ?, property}
constexpr uint32_t kLayerValue = 0x20;  // layer property: u16 layer
constexpr int kLayerType = 0xC;         // NiProperty vtbl+48h type of the layer property
using PropTypeFn = int (*)(const uint8_t* prop);
// The game's own type query (vtbl+48h, thiscall, as 0x534B30 does), -1 on a fault.
int GamePropType(const uint8_t* prop);
// The first property of obj's list whose typeOf is `type`; nullptr if none (or on a fault). At most 64 nodes.
uint8_t* FindProperty(const uint8_t* obj, int type, PropTypeFn typeOf);
// Copies the layer of `from` onto `to`: 1 = written (*was = the old value), 0 = already equal, -1 = one of
// them has no layer property. *value = from's layer when found.
int CopyLayer(const uint8_t* from, uint8_t* to, PropTypeFn typeOf, uint16_t* value, uint16_t* was);
}  // namespace rs7asset

// Startup: [assets] headstock7 / headstock7_file / dir / suffix (read once). Sets g_rs7_asset_on.
void Rs7Asset_Configure(const wchar_t* iniPath);
// The edited file for a game resource name ("data/assets/gameplay/guitar_x/foo.nif" ->
// dir + "foo" + suffix + ".nif"); empty when the name is not a guitar .nif or is unsafe. Pure.
std::wstring Rs7Asset_EditedPath(const char* name, const std::wstring& dir, const std::wstring& suffix);

// One headstock layout (a value of Optionen > Spieleinstellungen > Gitarrenkopfplatte) whose frame the
// 7-string in-line model replaces. folder: the package folder of the frames (with slashes); stockRh/stockLh:
// the name of the stock part under the frame's "Scene Root" (the one Show() culls). No attach transform: the
// seat (nut and string spans) is the same in every row, see the table in assetload_paths.cpp.
// peg: base name of the option's own peg model when it is not the in-line one (3+3: peg_bakelite); the
// handler merges the in-line peg into it and Show() swaps it per peg of a 7-string guitar. nullptr: in-line.
struct HeadLayout {
    const char* option;
    const char* folder;
    const char* stockRh;
    const char* stockLh;
    const char* peg;
};

extern const HeadLayout kLayouts[];
constexpr int kLayoutCount = 2;
// The row whose folder a game resource name is directly in (a .nif; case and slash style free) and
// whether it is the left-handed twin (base name ends in _lh); nullptr: not a candidate. Pure.
const HeadLayout* Rs7Asset_LayoutOf(const char* name, bool* leftHanded);
// Whether the 7-string model file (headstock7_file / _lh) applies to this game resource name: a .nif
// directly in a layout's folder, right-handed (base name without _lh) or left-handed. The frame check
// (node names of the stock tree) comes on top in the handler. Pure.
bool Rs7Asset_IsRhCandidate(const char* name);
bool Rs7Asset_IsLhCandidate(const char* name);
// The non-in-line peg model of a layout row (3+3: guitar_standard/peg_bakelite.nif, directly in the row's
// folder, case and slash style free). Pure.
bool Rs7Asset_IsLayoutPeg(const char* name);
// PGH1 (Rnd_StringRenderer_PegHighlightPass): 1 if the peg mesh or an ancestor is culled and is rs7_alt or a
// sibling of it (a 3+3 peg part hidden by the in-line swap), else 0. Reads only node fields.
extern "C" __declspec(dllexport) int __cdecl Rs7Asset_PegMeshHidden(const uint8_t* mesh);
// Snapshot a self-built path only after the builder publishes state 2. While untried/running,
// returns empty without reading the string, which may still be changing on another loader thread.
std::wstring Rs7Asset_PublishedPath(const std::wstring& path, volatile long* state);
// One owner for build output shared by several jobs (the right-handed rs7hs_*.dds that the left-handed model also
// references): the first caller runs `build`, every other caller waits (at most waitMs) for its result instead of
// writing the same intermediate files concurrently. state: 0 untried, 1 building, 2 built ok, 3 failed. Returns
// whether the shared output is available; a waiter that times out returns false and builds nothing. Never throws.
bool Rs7Asset_RunOnce(volatile long* state, bool (*build)(void*), void* context, unsigned waitMs);
// headstock7_file value -> the path ("" for none/off/0/empty; a relative name is taken relative to
// dir). Pure.
std::wstring Rs7Asset_ResolveFile(const std::wstring& value, const std::wstring& dir);
// [assets] dir value -> the folder. "" = kDefaultDir; an absolute value is kept; a relative one is
// taken relative to dllDir (the folder of rs7string.ini, which is the DLL's folder). Pure.
std::wstring Rs7Asset_ResolveDir(const std::wstring& value, const std::wstring& dllDir);
// Every distinct texture file name (".dds", case-insensitive, base name only) named in a NIF's bytes,
// in the order of first appearance. Pure.
std::vector<std::string> Rs7Asset_DdsNames(const uint8_t* buf, size_t size);
// "data/assets/x/model.nif" -> "data/assets/x/" (up to and including the last '/' or '\'). Pure.
std::string Rs7Asset_FolderOf(const char* name);
// Darkens a DXT1 (BC1) .dds in place by k (0 < k < 1): both RGB565 end points of every block of every
// mip level are scaled; a block whose end-point order (4-colour c0 > c1 / 3-colour c0 <= c1 mode) would
// flip is swapped back with its indices remapped, so no block changes mode. False (untouched) for
// anything but a plain DXT1 .dds or k outside (0, 1). Pure.
bool Rs7Asset_ScaleDxt1(uint8_t* dds, size_t size, float k);
// Whether [assets] headstock7_wood applies to a texture of the 7-string model: the base colour of the
// wood ("wood" in the name, not a gloss / normal map). Pure.
bool Rs7Asset_IsWoodColour(const std::string& name);
