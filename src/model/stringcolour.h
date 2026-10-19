#pragma once

// String 6's derived colours, shared by the native colour matrix (features/colours/) and the native strings
// (features/strings/): pure functions over RGB in 0..1.
namespace rs7s7 {
constexpr float kDefaultDim = 76.0f / 255.0f;  // 0x4C: the stock dimmed value (state 2), see Rs7S7_DimColour
}  // namespace rs7s7

// The stock relation between a string's state 1 and state 2 colour (GameColorManager data, all 12
// strings of both colour modes): same hue and saturation, value (max channel) = dimValue.
// rgb black stays black.
void Rs7S7_DimColour(const float rgb[3], float dimValue, float out[3]);
// String 6's glow colour (state 3, colorPtr[mode][3][6]). The stock state-3 colours are the state-1 hues at full
// value (max channel 0xFF) and full saturation (FF4F5A -> FF0010, 1DACF9 -> 00A9FF; snapshot palette). A grey has
// no hue to saturate, so this keeps the saturation and only scales the [color] string6 colour to max channel 1
// (C8C8C8 -> white). Black stays black.
void Rs7S7_GlowColour(const float rgb[3], float out[3]);
