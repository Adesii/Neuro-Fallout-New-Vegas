#pragma once

// Engine side of the raycast path check (todos 45 and 48): the Havok-ray `PathProbe::World` the Walker sweeps with,
// the `NWalkerDetour` console switch for the detours, and `NWalkerProbe`, which measures what a ray costs and logs
// what rays hit from where the player stands. The policy is engine-free in src/Movement/PathProbe.*; evidence and the
// in-game checks are in docs/design/raycast-path-detours.md and docs/design/custom-navigation-grid.md.

#include "Movement/PathProbe.hpp"
#include <cstdint>
#include <string>

class PlayerCharacter;
class TESObjectREFR;
struct NVSEInterface;

namespace WalkerProbe {

// Detours are off until switched on from the console (`NWalkerDetour 1`): nothing of this has run in the game yet.
bool DetourEnabled();
// `NWalkerDetour 3`: no side-step; a blocked spot goes straight to the pathfinder as a place to route around.
bool RerouteOnly();
// The collision layer the probe rays are cast as (WorldUtils::Layer); line of sight unless the console changed it.
uint8_t RayLayer();

// Casts the probe's rays in the game world. Rays ignore the player's own collision group. A hit on an actor, a door
// or `target` is told apart by the reference behind it; everything else is "world".
class EngineWorld : public PathProbe::World {
public:
  EngineWorld(PlayerCharacter *player, TESObjectREFR *target);
  PathProbe::RayHit Cast(const PathProbe::Point3 &from, const PathProbe::Point3 &to) override;
  // On the navmesh of the cell the point lies in (always true when the console switched the requirement off).
  bool Walkable(const PathProbe::Point3 &point) override;
  int Rays() const { return m_rays; }

private:
  PlayerCharacter *m_player;
  TESObjectREFR *m_target;
  uint16_t m_group;
  int m_rays = 0;
};

// "0010ABCD "Name" (STATIC)" for a hit on a reference, "terrain or unnamed geometry (TERRAIN)" otherwise.
std::string DescribeHit(const PathProbe::RayHit &hit);

// Microseconds since an arbitrary start, for timing a burst of rays.
double NowMicroseconds();

// Registers NWalkerDetour and NWalkerProbe. Call after WalkerOverlay::RegisterCommands: opcodes follow registration
// order.
void RegisterCommands(NVSEInterface *nvse);

} // namespace WalkerProbe
