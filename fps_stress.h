#pragma once
#include "fps_model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Cached spatial detail and camera-driven independent motion. No per-frame noise allocation.
namespace fps {
constexpr int kSceneCount = 4;
inline int TextureWidth(int scene) { return scene >= 2 ? 4096 : kTextureWidth; }
inline int TextureHeight(int scene) { return scene >= 2 ? 2048 : kTextureHeight; }
inline std::uint32_t StressHash(unsigned x, unsigned y) {
    unsigned h = x * 374761393U + y * 668265263U + 0x1234567U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}
inline std::uint32_t StressPixel(int scene, unsigned x, unsigned y) {
    const auto fine = StressHash(x, y);
    if (scene == 3)
        return fine & 0x00ffffffU;
    const auto leaf = StressHash(x / 5, y / 7);
    const auto patch = StressHash(x / 37, y / 29);
    const int grain = static_cast<int>(fine & 127) - 63;
    const bool foliage = (patch & 3) != 0;
    const bool edge = (x + (leaf & 15)) % 9 == 0 || (y + ((leaf >> 8) & 15)) % 11 == 0;
    const int base = edge ? 28 : 100 + static_cast<int>((leaf >> 16) & 95);
    const auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    const int r = clamp(base + grain - (foliage ? 38 : 0));
    const int g = clamp(base + grain + (foliage ? 12 : -16));
    const int b = clamp(base + grain - (foliage ? 65 : 24));
    return static_cast<unsigned>(r << 16 | g << 8 | b);
}
inline std::uint32_t ScenePixel(int scene, unsigned x, unsigned y) {
    if (scene >= 2)
        return StressPixel(scene, x, y);
    int r, green, b;
    if (scene == 0) {
        const bool line = x % 64 < 3 || y % 64 < 3;
        const bool square = ((x / 64 + y / 64) & 1) != 0;
        r = green = b = line ? 235 : (square ? 52 : 24);
    } else {
        // Stable multi-scale texture, not new random noise in each frame.
        const int coarse = static_cast<int>(StressHash(x / 24, y / 24) & 31);
        const int detail = static_cast<int>(StressHash(x, y) & 15) - 7;
        const int brick = (y / 22 & 1) * 24;
        if (y < 260) {
            const int cloud = static_cast<int>(StressHash(x / 100, y / 60) & 31);
            r = 70 + cloud;
            green = 120 + cloud;
            b = 175 + cloud;
        } else if (y < 570) {
            const int building = static_cast<int>(StressHash(x / 180, 0) & 63);
            const int roof = 280 + building * 2;
            if (y < static_cast<unsigned>(roof)) {
                r = 82;
                green = 129;
                b = 180;
            } else if (((x + brick) % 48 < 2) || y % 22 < 2) {
                r = 58;
                green = 59;
                b = 60;
            } else if (x % 180 > 38 && x % 180 < 80 && y % 96 > 22 && y % 96 < 65) {
                r = 35 + detail;
                green = 64 + detail;
                b = 79 + detail;
            } else {
                r = 118 + coarse + detail;
                green = 92 + coarse + detail;
                b = 70 + coarse + detail;
            }
        } else {
            const bool stone = (StressHash(x / 32, y / 20) & 7) < 2;
            r = (stone ? 87 : 43) + coarse + detail;
            green = (stone ? 84 : 76) + coarse + detail;
            b = (stone ? 72 : 31) + coarse + detail;
            if (x % 220 < 9 && y < 820) {
                r = 74;
                green = 58;
                b = 36;
            }
            if (x % 220 > 20 && x % 220 < 100 && y < 670) {
                r = 27 + detail;
                green = 62 + coarse;
                b = 22 + detail;
            }
        }
    }
    return (std::clamp(r, 0, 255) << 16) | (std::clamp(green, 0, 255) << 8) | std::clamp(b, 0, 255);
}
struct StressOffset {
    int x, y;
};
inline StressOffset TileOffset(int scene, int col, int row, double yaw, double pitch) {
    const auto seed = StressHash(static_cast<unsigned>(col), static_cast<unsigned>(row));
    if (scene == 3) {
        // Quantize at 1/32 degree: moving replaces each patch; stopping freezes it.
        const auto keyX = static_cast<unsigned>(static_cast<int>(std::floor(yaw * 32)));
        const auto keyY = static_cast<unsigned>(static_cast<int>(std::floor(pitch * 32)));
        const auto h = StressHash(seed ^ keyX, keyY);
        return {static_cast<int>(h % 4096), static_cast<int>((h >> 16) % 2048)};
    }
    const double gain = 0.7 + (seed & 15) * 0.12;
    const double direction = (seed & 16) ? 1.0 : -1.0;
    return {
        WrapOffset(static_cast<int>(std::lround(yaw * 4096 / 360 * gain)) +
                       static_cast<int>(seed % 4096),
                   4096),
        WrapOffset(static_cast<int>(std::lround(pitch * 6 + yaw * direction * (2 + (seed & 7)))) +
                       static_cast<int>((seed >> 16) % 2048),
                   2048)};
}
} // namespace fps
