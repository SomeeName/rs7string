#pragma once
#include "features/headstock/pegset.h"
#include <cstdint>

// Per-guitar state of the 7-string headstock model (game thread) and the steps the head module (assetload_head.cpp)
// takes it through: the string segments (assetload_segs.cpp) and the pegs (assetload_pegs.cpp).
namespace rs7asset {
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

// Segments (assetload_segs.cpp)
void FindSegments(Head& h);  // the stock and rs7_alt string_size_<n> nodes
void BindAlt(Head& h);       // our own param holders for the alt's segments, once per frame instance
void PointParams(Head& h, bool toAlt);
void Colour7(Head& h);  // the 7th segment follows the 7th string's colour
void Layer7(Head& h);   // string_size_7 gets string_size_6's noteway draw layer

// Pegs (assetload_pegs.cpp)
extern volatile long g_pegSwapEver;  // a 3+3 peg swap happened at least once
void FindLocators(Head& h);
void SwapLocators(Head& h, bool toAlt);
void PlacePegs(Head& h);
void InlinePegs(Head& h);  // 3+3: the in-line peg in a 7-string guitar's peg clones
void RestorePegs(Head& h);
void StockPegs(Head& h);  // a stock guitar: recycled clones get their own peg back
}  // namespace rs7asset
