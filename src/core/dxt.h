#pragma once
#include <cstddef>
#include <cstdint>

// CPU reference decoder for DXT1/DXT5 (BC1/BC3), used only with [color] allow_inexact=1 (plan D4):
// GPUs round the interpolated palette entries differently, so the result is not bit-exact with what
// the game's GPU samples.
//   colour: 565 -> 888 by bit replication; c0 > c1 (or DXT5): c2 = (2*c0 + c1 + 1)/3,
//           c3 = (c0 + 2*c1 + 1)/3; DXT1 with c0 <= c1: c2 = (c0 + c1)/2, c3 = transparent black.
//   alpha (DXT5): a0 > a1: ai = ((8-i)*a0 + (i-1)*a1 + 3)/7, i = 2..7;
//                 else ai = ((6-i)*a0 + (i-1)*a1 + 2)/5, i = 2..5, a6 = 0, a7 = 255.
// Decodes one level into A8R8G8B8 (0xAARRGGBB). w/h: level size (any, blocks are 4x4, padded).
// srcPitch: bytes per row of blocks (LockRect Pitch). dxt5 = false: DXT1 (punch-through alpha in
// 3-colour mode). Returns false on a short source (nothing guaranteed written then).
bool Rs7_DecodeDxt(const uint8_t* src, size_t srcBytes, size_t srcPitch, unsigned w, unsigned h, bool dxt5,
                   uint32_t* out /* w*h */);
