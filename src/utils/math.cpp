#include "math.h"
#include "NiPoint.h"

namespace Math {
float GetDistance2D(NiPoint3 *posA, NiPoint3 *posB) {
  if (!posA || !posB)
    return 0.0f;

  float dx = posA->x - posB->x;
  float dy = posA->y - posB->y;

  return sqrtf(dx * dx + dy * dy);
}

// angle 1 has to be radians
float GetHeadingBetweenPoints(float x1, float y1, float angle1, float x2, float y2) {

  // Calculate the difference between the two angles
  float angleDifference = atan2f(x2 - x1, y2 - y1) * 180.0f / PI - angle1;

  // Normalize the angle difference to the range [-180, 180]
  while (angleDifference > 180.0f)
    angleDifference -= 360.0f;
  while (angleDifference < -180.0f)
    angleDifference += 360.0f;

  return ToRadians(angleDifference); // This is the heading from point A to point B relative to angle1
}

float ToRadians(float degrees) { return degrees * (PI / 180.0f); }

float ToDegrees(float radians) { return radians * (180.0f / PI); }

} // namespace Math
