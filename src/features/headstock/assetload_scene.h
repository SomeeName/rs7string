#pragma once
#include "features/headstock/assetload.h"
#include "features/scene/scene.h"
#include <cstdint>
#include <intrin.h>

// Scene-graph helpers shared by the assetload modules: NiAVObject / NiNode field offsets of the 2022 build, SEH-guarded
// reads and the lookups of the rs7_alt node and of named nodes. The modules: assetload_load (nif handler, loader
// threads), assetload_tex (textures), assetload_head / _segs / _pegs (per guitar, game thread).
namespace rs7asset {
constexpr uint32_t kFlags = 0x20;       // NiAVObject u16 flags, bit 0 APP_CULLED
constexpr uint32_t kChildren = 0xD8;    // NiNode children: base pointer
constexpr uint32_t kChildSlots = 0xDC;  // u16 slot count (max size)
constexpr uint32_t kName = 0x10;        // NiObjectNET name (NiFixedString handle = char*)
constexpr uint32_t kRefCount = 4;       // NiRefObject reference count

// Rnd_Guitar and its InstrumentInfo
constexpr uint32_t kGuitarStrings = 0x0C;  // the guitar's strings object
constexpr uint32_t kGuitarHead = 0x18;     // Rnd_Headstock*
constexpr uint32_t kGuitarInfo = 0x24;     // InstrumentInfo*
constexpr uint32_t kHeadFrame = 0x04;      // Rnd_Headstock: the frame entity
constexpr uint32_t kInfoInverted = 8;      // InstrumentInfo: InvertGuitarStrings
constexpr uint32_t kInfoLefty = 9;
constexpr uint32_t kInfoInline = 0x0A;

template <class T>
T Rd(const void* base, uint32_t off) {
    T v{};

    if (base) {
        rs7s7d::SafeCopy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    }

    return v;
}

// Counts a log line: true for the first `max` calls (the log counters are volatile longs).
inline bool Upto(volatile long* n, long max) {
    return _InterlockedIncrement(n) <= max;
}

inline void SetCulled(uint8_t* node, bool culled) {
    uint16_t f = Rd<uint16_t>(node, kFlags);
    const uint16_t want = uint16_t(culled ? (f | 1u) : (f & ~1u));

    if (want != f) {
        rs7s7d::SafeStore(node + kFlags, &want, sizeof want);
    }
}

// NiPointer-style reference counting of a game object (the object stays alive: no destructor runs here).
inline void AddRef(void* obj) {
    _InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<uint8_t*>(obj) + kRefCount));
}

inline void DropRef(void* obj) {
    _InterlockedDecrement(reinterpret_cast<volatile long*>(static_cast<uint8_t*>(obj) + kRefCount));
}

// Whether obj is a scene node or mesh (the kinds the headstock culling treats; effects stay).
inline bool IsNodeOrMesh(const uint8_t* obj) {
    return rs7s7d::IsKindOf(obj, "NiNode") || rs7s7d::IsKindOf(obj, "NiMesh");
}

// Calls fn(child) for every non-null child in the first 32 slots of an NiNode, until fn returns false.
template <class Fn>
void EachChild(const uint8_t* node, Fn fn) {
    const unsigned slots = Rd<uint16_t>(node, kChildSlots);
    uint8_t** kids = Rd<uint8_t**>(node, kChildren);

    for (unsigned i = 0; kids && i < slots && i < 32; ++i) {
        uint8_t* c = Rd<uint8_t*>(kids, 4 * i);

        if (c && !fn(c)) {
            return;
        }
    }
}

// The top object of a loaded nif resource (its NiStream's first top object), nullptr without one.
uint8_t* TopObject(const void* res);
// X_FIXSTR 0x91CBF0, SEH-guarded: the NiFixedString handle (+1 ref) for `text`, nullptr on a fault.
const char* MakeFixedString(const char* text);
// The rs7_alt node: breadth-first, first 3 levels below `root`.
uint8_t* FindAlt(uint8_t* root);
// The node called `want`: depth-first, at most 256 nodes.
uint8_t* FindByName(uint8_t* root, const char* want);
// Renames a node to `to` (the old name reference is leaked).
void Rename(uint8_t* node, const char* to);
}  // namespace rs7asset
