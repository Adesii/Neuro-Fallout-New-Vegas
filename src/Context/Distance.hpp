#pragma once

#include <string>

namespace Units {

// The one place a distance becomes text for Neuro. It prints game units today; todo 39 switches every message to
// metres by changing this function once the units-per-metre factor has a recorded source. Do not format distances
// anywhere else.
std::string FormatDistance(float gameUnits);

} // namespace Units
