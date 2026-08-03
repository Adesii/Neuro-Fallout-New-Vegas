#pragma once

#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
namespace Math {

const double PI = std::acos(-1.0);
float GetDistance2D(const NiPoint3 *a, const NiPoint3 *b);

float GetHeadingBetweenPoints(float x1, float y1, float angle1, float x2, float y2);

float ToRadians(float degrees);
float ToDegrees(float radians);

} // namespace Math
