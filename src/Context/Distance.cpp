#include "Distance.hpp"
#include <cmath>

namespace Units {

float ToMeters(float gameUnits) { return gameUnits / kUnitsPerMeter; }

std::string FormatDistance(float gameUnits) {
  if (!std::isfinite(gameUnits) || gameUnits < 0.0f)
    return "an unknown distance";
  const long meters = std::lround(ToMeters(gameUnits));
  if (meters < 1)
    return "less than 1 meter";
  return std::to_string(meters) + (meters == 1 ? " meter" : " meters");
}

} // namespace Units
