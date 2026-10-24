#include "Distance.hpp"
#include <cmath>

namespace Units {

std::string FormatDistance(float gameUnits) {
  if (!std::isfinite(gameUnits) || gameUnits < 0.0f)
    return "an unknown distance";
  return std::to_string(static_cast<long>(std::lround(gameUnits))) + " units";
}

} // namespace Units
