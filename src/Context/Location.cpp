#include "Location.hpp"

namespace Location {

std::string DescribePlace(const PlaceInput &input) {
  if (!input.cellName.empty())
    return "in " + input.cellName;
  if (input.interior)
    return "in an interior area";
  const Marker *nearest = nullptr;
  for (const auto &marker : input.markers) {
    if (marker.name.empty() || !(marker.distance <= kNearMarkerUnits))
      continue;
    if (!nearest || marker.distance < nearest->distance ||
        (marker.distance == nearest->distance && marker.name < nearest->name))
      nearest = &marker;
  }
  if (nearest)
    return "near " + nearest->name;
  return "in the wasteland";
}

void PlaceTracker::Reset() { *this = PlaceTracker(); }

std::optional<std::string> PlaceTracker::Update(const PlaceInput &input, float dt) {
  m_sinceAnnouncement += dt;
  const std::string phrase = DescribePlace(input);
  if (phrase == m_announced && input.spaceId == m_announcedSpace) {
    m_pending.clear();
    m_stable = 0.0f;
    return std::nullopt;
  }
  if (phrase != m_pending || input.spaceId != m_pendingSpace) {
    m_pending = phrase;
    m_pendingSpace = input.spaceId;
    m_stable = 0.0f;
    return std::nullopt;
  }
  m_stable += dt;
  if (m_stable < kStableSeconds)
    return std::nullopt;
  const bool sameSpace = !m_announced.empty() && input.spaceId == m_announcedSpace;
  if (sameSpace && m_sinceAnnouncement < kOutdoorCooldownSeconds)
    return std::nullopt; // keep waiting; the pending place stays as it is
  m_announced = phrase;
  m_announcedSpace = input.spaceId;
  m_pending.clear();
  m_stable = 0.0f;
  m_sinceAnnouncement = 0.0f;
  return "You are now " + phrase + ".";
}

} // namespace Location
