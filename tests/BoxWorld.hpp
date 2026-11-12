#pragma once
// A fake world for the ray-based movement tests (PathProbe, NavGrid): axis-aligned boxes on a ground slab.

#include "Movement/PathProbe.hpp"
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace TestWorld {

using PathProbe::HitKind;
using PathProbe::Point3;
using PathProbe::RayHit;

// A world of axis-aligned boxes. A ray that starts inside a box does not hit that box (like a convex Havok shape).
struct Box {
  Point3 low;
  Point3 high;
  HitKind kind = HitKind::World;
  uint32_t formId = 0;
};

class BoxWorld : public PathProbe::World {
public:
  // The ground: a slab with its top at z = 0, `halfSize` to every side of the origin.
  explicit BoxWorld(float halfSize = 100000.0f) {
    boxes.push_back({{-halfSize, -halfSize, -1000.0f}, {halfSize, halfSize, 0.0f}});
  }

  void Add(Point3 low, Point3 high, HitKind kind = HitKind::World, uint32_t formId = 0) {
    boxes.push_back({low, high, kind, formId});
  }

  RayHit Cast(const Point3 &from, const Point3 &to) override {
    ++casts;
    RayHit best;
    for (const Box &box : boxes) {
      float enter = 0.0f;
      float leave = 1.0f;
      bool inside = true;
      bool miss = false;
      const float origin[3] = {from.x, from.y, from.z};
      const float delta[3] = {to.x - from.x, to.y - from.y, to.z - from.z};
      const float low[3] = {box.low.x, box.low.y, box.low.z};
      const float high[3] = {box.high.x, box.high.y, box.high.z};
      for (int axis = 0; axis < 3 && !miss; ++axis) {
        if (origin[axis] < low[axis] || origin[axis] > high[axis])
          inside = false;
        if (std::abs(delta[axis]) < 1e-6f) {
          miss = origin[axis] < low[axis] || origin[axis] > high[axis];
          continue;
        }
        float t0 = (low[axis] - origin[axis]) / delta[axis];
        float t1 = (high[axis] - origin[axis]) / delta[axis];
        if (t0 > t1)
          std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        miss = enter > leave;
      }
      if (miss || inside || enter >= best.fraction)
        continue;
      best.hit = true;
      best.fraction = enter;
      best.kind = box.kind;
      best.formId = box.formId;
    }
    return best;
  }

  bool Walkable(const Point3 &point) override { return !walkable || walkable(point); }

  std::vector<Box> boxes;
  std::function<bool(const Point3 &)> walkable;
  int casts = 0;
};

} // namespace TestWorld
