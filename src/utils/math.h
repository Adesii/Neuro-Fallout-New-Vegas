#pragma once

#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include <cmath>
#include <numbers>
namespace Math {

inline constexpr double PI = std::numbers::pi;

// Horizontal distance between anything with x and y members (NiPoint3, PathPoint3). The 3D counterpart is
// WorldUtils::Distance3D.
template <class A, class B> float GetDistance2D(const A &a, const B &b) { return std::hypot(a.x - b.x, a.y - b.y); }

float GetHeadingBetweenPoints(float x1, float y1, float angle1, float x2, float y2);

float ToRadians(float degrees);
float ToDegrees(float radians);

} // namespace Math
