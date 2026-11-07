#pragma once

#include "Context/ObjectPriority.hpp"
#include "Gamebryo/NiPoint3.hpp"
#include <cstdint>
#include <functional>

class PlayerCharacter;
class TESObjectCELL;
class TESObjectREFR;

namespace WorldUtils {

// The reference for a numeric form id, or null when it is missing, not a reference, deleted or disabled.
TESObjectREFR *LookupReference(uint32_t formId);
float Distance3D(const NiPoint3 &left, const NiPoint3 &right);

// The one decision about what a reference is and whether Neuro can act on it: `query_nearby`, exploration and Walker's
// interaction all use it. Actors (living or dead) and the built-in world (doors, containers, terminals, activators,
// flora, furniture) are interactable; loose items are too unless they carry ExtraPrimitive (an invisible volume, not a
// thing you can pick up). Everything else is Category::None.
Objects::Classification ClassifyObject(TESObjectREFR *ref);

// How far a line-of-sight ray is trimmed at each end so the player's own body and the target's own collision do not
// block it. Combat's defaults suit shooting at range; Walker uses shorter trims for interaction within about a metre.
struct LineOfSightTrim {
  float start = 35.0f;
  float end = 60.0f;
};

// True when a Havok ray on the engine's line-of-sight layer from `from` to `to` hits something. False when it is clear
// or when no cast could be performed (a blockage that was not observed is never claimed).
bool RayBlocked(const NiPoint3 &from, const NiPoint3 &to);

// One line-of-sight check, with what it was based on, so a refusal can be logged and explained.
struct LineOfSightTrace {
  bool clear = false;
  bool reticle = false;   // the crosshair actor rests on the target, no ray needed
  bool hasCamera = false; // false: no usable camera, nothing was checked
  bool tooShort = false;  // within both trims: counts as clear
  NiPoint3 camera = {0.0f, 0.0f, 0.0f};
  NiPoint3 from = {0.0f, 0.0f, 0.0f}; // the trimmed ray
  NiPoint3 to = {0.0f, 0.0f, 0.0f};
  float hitFraction = 1.0f; // along from -> to; below 1 when blocked
};
LineOfSightTrace TraceLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                                  LineOfSightTrim trim = {});

// A clear ray from the camera (AimUtils::CameraPosition) to `targetPoint`, or the crosshair actor already resting on
// the target (the engine's own reticle pick can only land on something it can see). Rays shorter than both trims
// together count as clear. Sources and unverified details: docs/combat.md "Unverified" item 1.
bool HasLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                    LineOfSightTrim trim = {});
// Writes one warning line describing `trace`: both camera readings, the feet, the target point, and where the ray was
// blocked (distance from the camera and short of the target).
void LogLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                    const LineOfSightTrace &trace, const char *context);

// Visits each enabled, undeleted reference in the loaded interior cell, or the loaded exterior grid, exactly once
// (the player included). The cell passed is the one that holds the reference.
void ForEachLoadedReference(const std::function<void(TESObjectREFR *, TESObjectCELL *)> &visit);

} // namespace WorldUtils
