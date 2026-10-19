#pragma once
#include "features/colours/colours.h"
#include <cstddef>
#include <cstdint>
#include <d3d9.h>
#include <vector>

// Texture level I/O for the ramp copies (stock ramps and RSMods ramps): read a texture into images, build the
// copy's levels with the self-check, create the copy and write it with a read-back. Render thread only.
namespace rs7swap {
// Reads every level of `t` into `out` (sized to the level count; DXT1/5 are decoded). False: why.
bool ReadLevels(IDirect3DTexture9* t, uint32_t fmt, std::vector<Rs7Image>& out, char* why, size_t whyLen);

// Writes `b` into `ours` level by level, then reads it back and compares (the runtime self-check also
// covers what the texture really holds). False: why.
bool WriteAndCheck(IDirect3DTexture9* ours, const std::vector<Rs7Image>& b, char* why, size_t whyLen);

struct CheckedBuild {
    Rs7RampBuild build;
    Rs7RampCheck chk;
};

// Rs7Ramp_Build + Rs7Ramp_Verify. False (why) when v6-pure fails, or exact fails and `exactRequired`.
bool BuildChecked(const std::vector<Rs7Image>& stock, const uint32_t rgb[2], bool exactRequired, CheckedBuild& out,
                  char* why, size_t whyLen);

enum class CopyResult { Ok, CreateFailed, WriteFailed };

// A MANAGED texture of `d`'s size with `levels` levels in format `fmt`, filled with `data` and read back.
// WriteFailed: the texture is already released. why is set unless Ok.
CopyResult MakeCopy(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d, unsigned levels, D3DFORMAT fmt,
                    const std::vector<Rs7Image>& data, IDirect3DTexture9** out, char* why, size_t whyLen);

// After a fault: unlock the level a read or write left locked (SEH-guarded).
void UnlockAfterFault();
// Tests: forget the locked level without touching it.
void ForgetLocked();
}  // namespace rs7swap
