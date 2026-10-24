#pragma once

#include <string>

namespace Units {

// Game units per meter. Source: JohnnyGuitarNVSE `shared/Defines.hpp` (`dM2NI = 69.99125671386719`, "1 Meter to Ni");
// itr-nvse's game-to-Havok scale of 1/7 agrees (one Havok unit is a decimeter, so 7 units are 10 cm). Not yet measured
// in this game: see the in-game checklist in todo/to_verify/39-distances-in-metres.md.
inline constexpr float kUnitsPerMeter = 69.99125671386719f;

float ToMeters(float gameUnits);

// The one place a distance becomes text for Neuro: whole meters ("3 meters"), "less than 1 meter" below one. Internal
// thresholds and tolerances stay in game units; only text for Neuro goes through here. Do not format distances
// anywhere else.
std::string FormatDistance(float gameUnits);

} // namespace Units
