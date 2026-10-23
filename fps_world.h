#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace world {
constexpr double kPi = 3.14159265358979323846;
struct Vertex {
  float x, y, z, u, v;
};
struct Face {
  Vertex v[4];
  int material;
  float light;
};
struct Box {
  float x0, z0, x1, z1;
};
struct Camera {
  double x = 0, z = 12;
};
struct Mesh {
  std::vector<Face> faces;
  std::vector<Box> solids;
  void Quad(Vertex a, Vertex b, Vertex c, Vertex d, int material, float light) {
    faces.push_back({{a, b, c, d}, material, light});
  }
  void Cuboid(float x0, float y0, float z0, float x1, float y1, float z1,
              int mat, bool solid = true) {
    const float u = (x1 - x0) / 2, v = (y1 - y0) / 2, w = (z1 - z0) / 2;
    Quad({x0, y0, z1, 0, 0}, {x1, y0, z1, u, 0}, {x1, y1, z1, u, v},
         {x0, y1, z1, 0, v}, mat, .92f);
    Quad({x1, y0, z0, 0, 0}, {x0, y0, z0, u, 0}, {x0, y1, z0, u, v},
         {x1, y1, z0, 0, v}, mat, .72f);
    Quad({x0, y0, z0, 0, 0}, {x0, y0, z1, w, 0}, {x0, y1, z1, w, v},
         {x0, y1, z0, 0, v}, mat, .8f);
    Quad({x1, y0, z1, 0, 0}, {x1, y0, z0, w, 0}, {x1, y1, z0, w, v},
         {x1, y1, z1, 0, v}, mat, .65f);
    Quad({x0, y1, z1, 0, 0}, {x1, y1, z1, u, 0}, {x1, y1, z0, u, w},
         {x0, y1, z0, 0, w}, mat, 1.f);
    if (solid)
      solids.push_back({x0, z0, x1, z1});
  }
};
inline Mesh Build() {
  Mesh m;
  m.Quad({-22, 0, 22, 0, 0}, {22, 0, 22, 22, 0}, {22, 0, -22, 22, 22},
         {-22, 0, -22, 0, 22}, 0, 1.f);
  m.Quad({-22, 7, -22, 0, 0}, {22, 7, -22, 11, 0}, {22, 7, 22, 11, 11},
         {-22, 7, 22, 0, 11}, 2, .8f);
  m.Cuboid(-22, 0, -22, 22, 7, -21.8f, 1, false);
  m.Cuboid(-22, 0, -22, -21.8f, 7, 22, 1, false);
  m.Cuboid(21.8f, 0, -22, 22, 7, 22, 1, false);
  m.Cuboid(-22, 0, 21.8f, 22, 7, 22, 1, false);
  // Aisles give depth, a near column makes rotation and occlusion obvious.
  for (int z = -16; z <= 16; z += 8)
    for (int side : {-1, 1}) {
      const float x = side * 5.f;
      m.Cuboid(x - .48f, 0, static_cast<float>(z) - .48f, x + .48f, 6.5f,
               static_cast<float>(z) + .48f, 2);
      m.Cuboid(x - .65f, 0, static_cast<float>(z) - .65f, x + .65f, .22f,
               static_cast<float>(z) + .65f, 2, false);
      m.Cuboid(x - .51f, 1.2f, static_cast<float>(z) - .51f, x + .51f, 1.6f,
               static_cast<float>(z) + .51f, 3, false);
    }
  m.Cuboid(-3.7f, 0, 7.3f, -2.8f, 3.7f, 8.2f, 3);
  m.Cuboid(2.8f, 0, 3.5f, 4.4f, 1.7f, 5.1f, 2);
  for (int z = -16; z <= 16; z += 8)
    for (int side : {-1, 1}) {
      const float x = side * 12.f;
      m.Cuboid(x - 2, 0, static_cast<float>(z) - 1.5f, x + 2, 1.7f,
               static_cast<float>(z) + 1.5f, 2);
      m.Cuboid(x - 1.7f, 1.7f, static_cast<float>(z) - 1.2f, x + .4f, 3.2f,
               static_cast<float>(z) + .8f, 3, false);
    }
  // Distant target, horizontal beams and wall insets supply scale cues.
  m.Cuboid(-2.4f, 0, -21.6f, 2.4f, 4.2f, -21.3f, 3, false);
  for (int z = -16; z <= 16; z += 8)
    m.Cuboid(-5.5f, 6.2f, static_cast<float>(z) - .35f, 5.5f, 6.6f,
             static_cast<float>(z) + .35f, 2, false);
  return m;
}
inline Mesh BuildPressure(int pressure) {
  Mesh m = Build();
  if (pressure <= 0)
    return m;
  // Staggered thin rods and hanging bars create real depth/occlusion changes.
  // Keep the central walking aisle and the original collision volumes clear.
  const int step = pressure == 2 ? 2 : 4;
  for (int z = -18; z <= 18; z += step)
    for (int x = -18; x <= 18; x += step) {
      if (std::abs(x) < 2 || (std::abs(x) < 4 && z > 6))
        continue;
      const unsigned seed =
          static_cast<unsigned>((x + 23) * 7919 + (z + 23) * 104729);
      const float dx = static_cast<float>((seed >> 3) % 13) * .055f;
      const float height = .5f + static_cast<float>((seed >> 8) % 35) * .1f;
      const float width = pressure == 2 ? .045f : .08f;
      m.Cuboid(x + dx, 0, static_cast<float>(z), x + dx + width, height,
               z + width, static_cast<int>(seed % 4), false);
      m.Cuboid(x - .5f, 5.4f - height * .2f, z - .12f, x + .6f,
               5.44f - height * .2f, z + .12f,
               static_cast<int>((seed >> 4) % 4), false);
    }
  // Unique world-anchored UVs break the small repeated grid/brick tiling.
  unsigned i = 0;
  for (auto &face : m.faces) {
    const float offset = static_cast<float>((++i * 37) % 127) * .013f;
    for (auto &v : face.v) {
      if (std::abs(face.v[0].y - face.v[1].y) < .001f &&
          std::abs(face.v[0].y - face.v[2].y) < .001f) {
        v.u = v.x * .17f + offset;
        v.v = v.z * .17f - offset;
      } else {
        v.u = (v.x + v.z) * .17f + offset;
        v.v = v.y * .17f - offset;
      }
    }
  }
  return m;
}
inline bool Free(const Mesh &m, double x, double z) {
  constexpr double radius = .28;
  if (std::abs(x) > 21.3 || std::abs(z) > 21.3)
    return false;
  for (const auto &b : m.solids)
    if (x > b.x0 - radius && x < b.x1 + radius && z > b.z0 - radius &&
        z < b.z1 + radius)
      return false;
  return true;
}
inline void Move(const Mesh &m, Camera &c, double yaw, double forward,
                 double right, double dt) {
  dt = std::clamp(dt, 0., .05);
  const double norm = std::max(1., std::hypot(forward, right)),
               a = yaw * kPi / 180.;
  const double dx =
      (std::sin(a) * forward + std::cos(a) * right) * dt * 4 / norm;
  const double dz =
      (-std::cos(a) * forward + std::sin(a) * right) * dt * 4 / norm;
  if (Free(m, c.x + dx, c.z))
    c.x += dx;
  if (Free(m, c.x, c.z + dz))
    c.z += dz;
}
} // namespace world
