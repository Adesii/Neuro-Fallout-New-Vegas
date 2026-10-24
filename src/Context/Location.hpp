#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Location {

// A discovered map marker among the loaded references, with its straight-line distance from the player in game units.
struct Marker {
  std::string name;
  float distance = 0.0f;
};

struct PlaceInput {
  bool interior = false;
  uint32_t spaceId = 0; // the interior cell or the worldspace
  std::string cellName; // the cell's own name; empty for most wilderness cells
  std::vector<Marker> markers;
};

// How far a marker may be and still count as "near" (about 300 m, game units; todo 28 suggested it, to be tuned in
// game).
inline constexpr float kNearMarkerUnits = 21000.0f;

// The place as a phrase that follows "You are ...": the cell's own name when it has one ("in Goodsprings Saloon"),
// otherwise "near <nearest marker within kNearMarkerUnits>", otherwise "in the wasteland" (outdoors) or "in an interior
// area" (an interior cell with no name). Empty marker names are ignored.
std::string DescribePlace(const PlaceInput &input);

// Decides when a location line is worth sending: the place must be unchanged for kStableSeconds (load screens and cell
// crossings flap), differ from the one last announced, and a move that stays in the same space waits for
// kOutdoorCooldownSeconds so walking along a settlement's edge does not chatter. A different space (a door, a load)
// announces as soon as it is stable. Engine-free (tests/LocationTests.cpp).
class PlaceTracker {
public:
  static constexpr float kStableSeconds = 1.5f;
  static constexpr float kOutdoorCooldownSeconds = 30.0f;

  void Reset();
  // `dt` is the time since the previous call. Returns the line to send, if any.
  std::optional<std::string> Update(const PlaceInput &input, float dt);
  // The phrase last announced; empty before the first announcement.
  const std::string &Announced() const { return m_announced; }

private:
  std::string m_announced;
  uint32_t m_announcedSpace = 0;
  std::string m_pending;
  uint32_t m_pendingSpace = 0;
  float m_stable = 0.0f;
  float m_sinceAnnouncement = 0.0f;
};

} // namespace Location
