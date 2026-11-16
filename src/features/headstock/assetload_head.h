#pragma once
#include "features/headstock/pegset.h"
#include <cstdint>

// Per-guitar state of the 7-string headstock model (game thread) and the steps the head module (assetload_head.cpp)
// takes it through: the string segments (assetload_segs.cpp) and the pegs (assetload_pegs.cpp).
namespace rs7asset {
constexpr int kSegs = 6;   // stock headstock string segments (param objects Rnd_Headstock +20h + s*4)
constexpr int kExtra = 2;  // strings beyond the stock six: string_size_7/8, Locator_Peg_7/8 (index e = string 6 + e)
constexpr uint32_t kHeadLoaded = 0x18;  // u8 per segment: loaded (Rnd_Headstock_LoadStringMeshes 0x58E090)
constexpr uint32_t kHeadParams = 0x20;
constexpr uint32_t kColourRgb =
    0x14;  // g_diffuseColor extra data: RGB floats (Rnd_Mesh_SetDiffuseColorExtraData 0x7AF860)

struct Head {
    uint8_t* guitar;
    uint8_t* root;     // frame entity root the alt was found under
    uint8_t* alt;      // the rs7_alt clone (nullptr: none in this frame)
    bool alt5;         // alt is rs7_alt5 (a bass built with five strings, a guitar with seven next to the 8-peg model)
    bool bass;         // FindSegments: the instrument is a bass (segment names count - s + 6)
    uint8_t* sibs[8];  // stock siblings we culled
    int nsibs;
    bool applied;  // alt shown
    bool bound;    // the param objects point at the alt's segments
    int frames;
    uint8_t* stockSeg[kSegs];       // slot s -> string_size_<6-s> of the stock model
    uint8_t* altSeg[kSegs];         // the same names in rs7_alt
    uint8_t* segX[kExtra];          // rs7_alt's string_size_7 / _8 (no stock slot; nullptr: not in the model)
    uintptr_t paramsX[kExtra][9];   // our own param holders for segX (same layout as the stock ones)
    uintptr_t altHold[kSegs][9];    // the alt segments' extra data (our holders)
    uintptr_t stockHold[kSegs][9];  // the stock holders' pointers (one reference each, ours)
    bool segOk[kSegs];
    uint8_t loadedWas[kSegs];  // the loaded flags PointParams(toAlt) replaced
    bool pointed;              // the stock param objects point at the alt's extra data
    uint8_t* stockLoc[kSegs];  // PegSet slot s: the stock Locator_Peg_<s+1> (or _Invert_) node
    uint8_t* altLoc[kSegs];    // the same names in rs7_alt
    uint8_t* altLocX[kExtra];  // rs7_alt's Locator_Peg_7 / _8 (or _Invert_): PegSet slots 6 / 7
    bool locSwapped;
    bool texChecked;
    bool onNonInline;                      // the frame is not the in-line one (InstrumentInfo +0Ah = 0, 3+3): LT1, pegs
    uint8_t* pegDone[rs7head::kCapacity];  // peg entities whose clone shows the merged in-line peg (3+3)
    uint8_t* pegCheckedSet;  // a stock guitar's PegSet whose pegs were checked for a left-over in-line peg
};

// Segments (assetload_segs.cpp)
void FindSegments(Head& h);   // the stock and rs7_alt string_size_<n> nodes
void BindAlt(Head& h);        // our own param holders for the alt's segments, once per frame instance
void ReleaseParams(Head& h);  // release all owned holder references before rebind or slot removal
void PointParams(Head& h, bool toAlt);
void ColourExtra(Head& h, int e);  // extra segment e follows its string's colour (non-native strings)
void LayerExtra(Head& h, int e);   // extra segment e gets string_size_6's noteway draw layer
void LayerBass(Head& h);           // a bass's added segments get string_size_10's

// Extra segment e is shown only on a guitar built with more than 6 + e strings (pure).
inline bool ExtraShown(int e, unsigned strings) {
    return strings > unsigned(kSegs + e);
}

// The edited bass model's segment for string s (pure): the stock E A D G names string_size_10 .. 7, then the native
// bass's added strings (features/bass) by gauge, low B (string 4) string_size_11 and high C (string 5) string_size_6
// (rs7headstock design.py string_mesh_name).
inline int BassSegName(int s) {
    return s == 4 ? 11 : s == 5 ? 6 : 10 - s;
}

// Pegs (assetload_pegs.cpp)
void FindLocators(Head& h);
void SwapLocators(Head& h, bool toAlt);
void KeepExtraLocators(Head& h);  // every shown frame: seated extra slots on the shown model's locators
void PlacePegs(Head& h);
void InlinePegs(Head& h);  // 3+3: the in-line peg in a 7-string guitar's peg clones
void RestorePegs(Head& h);
void StockPegs(Head& h);  // a stock guitar: recycled clones get their own peg back
}  // namespace rs7asset
