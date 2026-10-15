#pragma once
#include <cstdint>

// String 6's default colour: light grey (owner's choice), 0xRRGGBB.
constexpr uint32_t kRs7DefaultColour = 0xC8C8C8;

// "C8C8C8", "#c8c8c8", surrounding blanks/tabs ok; exactly six hex digits. *rgb untouched on failure.
bool Rs7_ParseRgb(const wchar_t* text, uint32_t* rgb);
// out = {R, G, B} / 255.0f.
void Rs7_RgbToFloats(uint32_t rgb, float out[3]);
// One channel of the grey ramp line g recoloured to colour c around anchor a (plan D8):
// g <= a: (c*g + a/2)/a; else c + ((255-c)*(g-a) + (255-a)/2)/(255-a). Identity when c == a.
uint8_t Rs7_Recolor(uint8_t g, uint8_t anchor, uint8_t c);
