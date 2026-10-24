#pragma once

#include "Context/ObjectPriority.hpp"
#include <cstdint>
#include <functional>

class NiPoint3;
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

// A clear ray from the player's eye (stance-aware) to `targetPoint`, or the crosshair actor already resting on the
// target (the engine's own reticle pick can only land on something it can see). Rays shorter than both trims together
// count as clear. Sources and unverified details: docs/combat.md "Unverified" item 1.
bool HasLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                    LineOfSightTrim trim = {});

// Visits each enabled, undeleted reference in the loaded interior cell, or the loaded exterior grid, exactly once
// (the player included). The cell passed is the one that holds the reference.
void ForEachLoadedReference(const std::function<void(TESObjectREFR *, TESObjectCELL *)> &visit);

} // namespace WorldUtils
