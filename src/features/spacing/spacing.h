#pragma once
#include <cstdint>

// String spacing of extended instruments ([neck] spacing_bass5 / spacing_bass6 / spacing_guitar7 /
// spacing_guitar8): the distance between neighbouring strings on the neck and highway, in world units. 0 = the
// stock spacing of the instrument (guitar 1.608, bass 2.680). The stock 4-string bass and 6-string guitar are
// both 8.04 wide; a 6-string bass at the stock bass spacing is 13.4 wide.
//
// Every string position comes from Rnd_GetStringAttachPoint 0x594530 (string meshes, FretGrid cells and fret spans;
// CONFIRMED callers 0x594DC8 / 0x594F36 / 0x59A5E9). Its entry hook PUBGA (features/publish) answers with
// Rs7Gs_AttachSpaced (features/strings): the stock point scaled about the centre of the stock rows, so the fret bars,
// floor and chord frames, which derive from the attach points, follow. Read when the instrument is built.

struct Rs7SetTable;
struct Rs7UiTable;

namespace rs7spacing {
constexpr float kMin = 0.1f;  // the smallest spacing other than 0 (stock)
constexpr float kMax = 4.0f;

// The spacing setting for an instrument (bass: InstrumentInfo +0 == 2; count = its build count +0Bh), 0 = stock.
// Only extended instruments (5/6-string bass, 7/8-string guitar) have one.
inline float Target(const float set[4], bool bass, uint32_t count) {
    if (bass) {
        return count == 5 ? set[0] : count == 6 ? set[1] : 0.0f;
    }

    return count == 7 ? set[2] : count == 8 ? set[3] : 0.0f;
}

// A spacing below kMin is stock (0).
inline float OrStock(float v) {
    return v >= kMin ? v : 0.0f;
}

// p' = c + k (p - c).
inline void Scale(const float c[3], float k, float p[3]) {
    for (int i = 0; i < 3; ++i) {
        p[i] = c[i] + k * (p[i] - c[i]);
    }
}

// An ini value (the settings rows' range 0 .. kMax): empty or below kMin = stock (0). False for text that is not such a
// number (*out = 0, stock).
bool Parse(const wchar_t* text, float* out);
}  // namespace rs7spacing

extern "C" {
// bass5, bass6, guitar7, guitar8 (settings.cpp; read on the instrument build).
__declspec(dllexport) extern float g_rs7_spacing[4];
}

// The factor (new / stock spacing) an InstrumentInfo's attach points were scaled by (spacing.cpp): recorded at the
// build by Rs7Gs_AttachSpaced, read every frame by the headstock (features/headstock Rs7Head_Width), which narrows the
// frame by it so its string segments meet the neck strings at the nut. 1 = stock.
void Rs7Spacing_Record(const uint8_t* info, float k);
float Rs7Spacing_Factor(const uint8_t* info);
// Back to 1 for a recorded info (a stock build); writes nothing when there is no such entry (the stock attach path).
void Rs7Spacing_Forget(const uint8_t* info);

extern const Rs7SetTable kRs7SpacingSettings;
extern const Rs7UiTable kRs7SpacingUi;
