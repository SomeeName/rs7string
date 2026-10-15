#pragma once
#include "core/rampstate.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Gamebryo's global NiTexture list (plan D2): every engine texture is linked into it by the base
// constructor under EnterCriticalSection(X_TCS); head at X_THEAD. September 2022 exe only (the PE
// checksum gate in Rs7_Startup); tools/tests/test_snapshot_texlist.py pins these in the snapshot.
namespace rs7tex {                            // CONFIRMED in the snapshot (plan "Offline findings")
constexpr uint32_t kVtSource54 = 0x011EA2A0;  // Gfx_SourceTexture_CreateFromFile objects
constexpr uint32_t kVtSource50 = 0x011E8C58;  // FUN_009eac60 / NIF source textures
constexpr uint32_t kVtRendData = 0x0121A688;  // renderer data of a source texture
constexpr uint32_t kNext = 0x34, kRendData = 0x2C, kFile = 0x3C, kPlatFile = 0x40;
constexpr uint32_t kRdOwner = 0x10, kRdW = 0x14, kRdH = 0x18, kRdFmt = 0x28, kRdD3d = 0x64;
constexpr unsigned kMaxNodes = 8192;
}  // namespace rs7tex

// Memory access for the walk; returns false instead of faulting (the DLL's reader is SEH-guarded).
struct Rs7MemReader {
    virtual bool Read(uintptr_t a, void* out, size_t n) const = 0;
};

struct Rs7TexFound {
    uintptr_t obj, rd, d3d;
    Rs7Family family;
    uint32_t fmt;
    char path[120];
};

struct Rs7TexWalk {
    unsigned nodes = 0, sources = 0, nameMatches = 0;
    bool cycleOrCap = false, readFault = false;
    std::vector<Rs7TexFound> found;
    std::vector<std::string> rejects;  // "obj=%08X '<path>': <check> (<value>)" for name matches that fail
    uintptr_t anyD3d = 0;              // first valid d3d texture of any name (device acquisition)
    std::vector<uint32_t> seen;        // scratch: cycle detection table (Rs7Tex_Prepare sizes it)
};

// note_gradient_map.dds -> Note, gradient_map_additive.dds -> Additive: basename after the last
// / or \, ASCII case-insensitive. False for anything else (and for nullptr).
bool Rs7Tex_FamilyOf(const char* filename, Rs7Family* f);
// Walks from *head. d3d9Lo/Hi: image range of d3d9.dll (vtable check). Does NOT lock: the caller holds
// the game's list lock (X_TCS).
//   nodes: list nodes read; sources: nodes with a source-texture vtable (only these are examined);
//   nameMatches: sources whose file name (+0x3C), or else platform file name (+0x40), is a ramp;
//   readFault: the list itself (head, a node's vtable or link) was unreadable; cycleOrCap: a node
//   repeated or kMaxNodes reached. Both stop the walk; what was found before stays valid.
// A name match is added to `found` only if: renderer data non-null with vtable kVtRendData and owner ==
// obj, 256x128, d3d non-null with its vtable in [d3d9Lo, d3d9Hi). Otherwise one reject line.
// path: the platform file name if readable and non-empty, else the file name (NUL within 120 bytes).
Rs7TexWalk Rs7Tex_Walk(const Rs7MemReader& mem, uintptr_t headAddr, uintptr_t d3d9Lo, uintptr_t d3d9Hi);
// Allocates the walk's storage (found/rejects capacity for 64 entries, the cycle table) so that
// Rs7Tex_WalkInto, which runs under the game's list lock, does not allocate (except one string per
// reject line and more than 64 ramp matches).
void Rs7Tex_Prepare(Rs7TexWalk* w);
// Rs7Tex_Walk into existing storage (cleared first, capacity kept).
void Rs7Tex_WalkInto(const Rs7MemReader& mem, uintptr_t headAddr, uintptr_t d3d9Lo, uintptr_t d3d9Hi, Rs7TexWalk* w);
// Render thread, under the list lock, before taking a reference on a candidate: true iff `obj` is still
// a source texture whose file name (or platform file name) is a ramp of family `fam`, whose renderer
// data passes the walk's checks (vtable, owner, 256x128, d3d vtable in [d3d9Lo, d3d9Hi)) and still
// holds `d3d` at +0x64. Catches an address reused after the engine freed the texture. No allocation.
bool Rs7Tex_Revalidate(const Rs7MemReader& mem, uintptr_t obj, uintptr_t d3d, Rs7Family fam, uintptr_t d3d9Lo,
                       uintptr_t d3d9Hi);
