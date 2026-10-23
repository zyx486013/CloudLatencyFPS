#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Portable motion math. Texture contents stay fixed; only their offset changes.
namespace fps {
constexpr int kTextureWidth = 2048, kTextureHeight = 1024;
constexpr int kBundleColumns = 4, kBundleRows = 2, kFields = 8;
constexpr int kHistory = 4096;
inline std::uint16_t BundleCheck(const std::uint16_t *fields) {
    std::uint16_t crc = 0xFFFF;
    for (int i = 0; i < 7; ++i)
        for (int byte = 1; byte >= 0; --byte) {
            crc ^= static_cast<std::uint16_t>(((fields[i] >> (byte * 8)) & 255) << 8);
            for (int bit = 0; bit < 8; ++bit)
                crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    return crc & 8191;
}
struct Pose {
    double yaw = 0, pitch = 0;
    int phase = 0;
};
inline double Triangle(double seconds, double period, double amplitude) {
    const double p = seconds / period - std::floor(seconds / period);
    return amplitude * (p < .25 ? p * 4 : (p < .75 ? 2 - p * 4 : p * 4 - 4));
}
inline Pose Automatic(double seconds) {
    if (seconds < 10)
        return {};
    if (seconds < 30)
        return {Triangle(seconds - 10, 10, 45), 0, 1};
    if (seconds < 50)
        return {Triangle(seconds - 30, 4, 90), 0, 2};
    return {0, 0, seconds < 60 ? 3 : 4};
}
inline int WrapOffset(int value, int modulus) { return (value % modulus + modulus) % modulus; }
inline std::uint16_t EncodeYaw(double degrees) {
    degrees = degrees - std::floor(degrees / 360.0) * 360.0;
    return static_cast<std::uint16_t>(std::lround(degrees * 8192.0 / 360.0)) & 8191;
}
inline double DecodeYaw(std::uint16_t value) { return (value & 8191) * 360.0 / 8192.0; }
inline double YawDelta(double previous, double current) {
    double d = current - previous;
    return d - std::floor((d + 180.0) / 360.0) * 360.0;
}
inline std::uint16_t EncodePitch(double degrees) {
    return static_cast<std::uint16_t>(
        std::lround((std::clamp(degrees, -80.0, 80.0) + 80) * 8191.0 / 160.0));
}
inline double DecodePitch(std::uint16_t value) { return (value & 8191) * 160.0 / 8191.0 - 80.0; }
// flags: [0]=manual [1..2]=scene [3..5]=phase [6..8]=rate [9]=capture.
// [10..12]=renderer profile tag: 2=standard, 3=3D high, 4=3D extreme.
inline std::uint16_t Flags(bool manual, int scene, int phase, int rateIndex, bool capture, int pressure = 0) {
    return static_cast<std::uint16_t>(((2 + (scene < 2 ? std::clamp(pressure,0,2) : 0)) << 10) | (manual ? 1 : 0) | ((scene & 3) << 1) |
                                      ((phase & 7) << 3) | ((rateIndex & 7) << 6) |
                                      (capture ? 512 : 0));
}
struct Point {
    double time = 0, position = 0;
};
struct Match {
    double latency = 0, correlation = 0, gain = 0;
    bool valid = false;
};
// Caller provides chronological, unwrapped yaw and local cumulative raw X data.
// Compare nonuniform reversal/acceleration segments; constant velocity is rejected.
inline Match MatchTracking(const Point *local, int localCount, const Point *remote,
                           int remoteCount) {
    Match best;
    if (localCount < 30 || remoteCount < 30 || remote[remoteCount - 1].time - remote[0].time < 1000)
        return best;
    double correlations[251]{};
    for (int delay = 0; delay <= 500; delay += 2) {
        int j = 0, n = 0;
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        for (int i = 0; i < remoteCount; ++i) {
            const double t = remote[i].time - delay;
            while (j + 1 < localCount && local[j + 1].time < t)
                ++j;
            if (j + 1 >= localCount || t < local[j].time || local[j + 1].time - local[j].time > 80)
                continue;
            const double span = local[j + 1].time - local[j].time;
            if (span <= 0)
                continue;
            const double x = local[j].position + (local[j + 1].position - local[j].position) *
                                                     (t - local[j].time) / span;
            const double y = remote[i].position;
            ++n;
            sx += x;
            sy += y;
            sxx += x * x;
            syy += y * y;
            sxy += x * y;
        }
        const double vx = n * sxx - sx * sx, vy = n * syy - sy * sy, cov = n * sxy - sx * sy;
        if (n < 30 || vx < 1 || vy < 1)
            continue;
        const double r = cov / std::sqrt(vx * vy);
        correlations[delay / 2] = r;
        if (r > best.correlation)
            best = {static_cast<double>(delay), r, cov / vx, false};
    }
    // A flat correlation peak (e.g. constant velocity) cannot identify latency.
    const int index = static_cast<int>(best.latency) / 2;
    double alternative = -1;
    for (int i = 0; i <= 250; ++i)
        if (std::abs(i - index) >= 25)
            alternative = std::max(alternative, correlations[i]);
    best.valid = best.correlation >= .9 && best.gain > 0 &&
                 best.correlation - alternative >= .015 && index > 0 && index < 250;
    return best;
}
} // namespace fps
