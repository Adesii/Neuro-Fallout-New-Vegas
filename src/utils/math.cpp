#include "math.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Movement/AimLogic.hpp"

namespace Math {
float GetDistance2D(const NiPoint3 *posA, const NiPoint3 *posB) {
  if (!posA || !posB)
    return 0.0f;

  float dx = posA->x - posB->x;
  float dy = posA->y - posB->y;

  return sqrtf(dx * dx + dy * dy);
}

// Signed heading from (x1, y1) towards (x2, y2) relative to a facing of angle1, in radians.
// angle1 is the current facing in DEGREES; the returned heading is normalised to [-pi, pi].
float GetHeadingBetweenPoints(float x1, float y1, float angle1, float x2, float y2) {

  // Calculate the difference between the two angles
  float angleDifference = atan2f(x2 - x1, y2 - y1) * 180.0f / PI - angle1;

  // Normalize the angle difference to the range [-180, 180]; non-finite input gives 0 instead of looping.
  angleDifference = AimLogic::WrapDegrees(angleDifference);

  return ToRadians(angleDifference); // This is the heading from point A to point B relative to angle1
}

float ToRadians(float degrees) { return degrees * (PI / 180.0f); }

float ToDegrees(float radians) { return radians * (180.0f / PI); }

} // namespace Math
