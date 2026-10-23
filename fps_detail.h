#pragma once
#include "fps_stress.h"
namespace detail {
// Stable spatial detail. Every mip level has its own cached multi-scale
// pattern, retaining contrast at distance instead of averaging all fine detail
// to gray.
inline void Pixel(int scene, int pressure, int material, unsigned x, unsigned y,
                  int level, unsigned char *rgb) {
  const unsigned seed =
      static_cast<unsigned>(material * 197 + level * 1237 + scene * 389);
  const auto h2 = fps::StressHash(x / 2 + seed, y / 2 + seed * 3);
  const auto h7 = fps::StressHash(x / 7 + seed * 7, y / 7 + seed);
  const auto h29 = fps::StressHash(x / 29 + seed, y / 29 + seed * 11);
  const auto fine = fps::StressHash(x + seed, y + seed);
  int luma;
  if (pressure == 1)
    luma = 12 + static_cast<int>(h2 & 127) + static_cast<int>(h7 & 95) +
           static_cast<int>(h29 & 15);
  else
    luma = static_cast<int>(fine & 255);
  const bool scratch =
      (x + (h29 & 31)) % 43 < 2 || (y + ((h29 >> 8) & 31)) % 61 < 2;
  if (scratch)
    luma = 235;
  if (scene == 0 && ((x % 32 == 0) || (y % 32 == 0)))
    luma = 250;
  const int grain = static_cast<int>(fine & 15) - 7;
  const int tint = pressure == 2 ? static_cast<int>((fine >> 8) & 127) - 63 : 0;
  rgb[0] = static_cast<unsigned char>(
      std::clamp(luma + grain + tint + (material == 1 ? 12 : 0), 0, 255));
  rgb[1] = static_cast<unsigned char>(
      std::clamp(luma + grain + (material == 0 ? 10 : 0), 0, 255));
  rgb[2] = static_cast<unsigned char>(
      std::clamp(luma + grain - tint - (material == 1 ? 15 : 0), 0, 255));
}
} // namespace detail
