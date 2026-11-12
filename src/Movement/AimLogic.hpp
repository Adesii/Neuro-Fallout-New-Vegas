#pragma once

// Engine-independent aim maths shared by Walker and Combat (todo 27): angle wrapping, the one height table for "where
// on this thing do I look", and the checks that decide whether an engine reading (camera, mesh bound, skeleton node)
// is usable as an aim point. AimUtils feeds it engine reads.

namespace AimLogic {

// Wraps to [-pi, pi]. Non-finite input (NaN, +/-inf) returns 0 instead of looping or propagating.
float WrapAngle(float radians);
// The same for degrees, to [-180, 180].
float WrapDegrees(float degrees);

enum class TargetKind {
  Character, // humanoid actor
  Creature,  // non-humanoid actor
  Activator, // door, container, terminal, furniture, talking activator
  Flora,
  Object, // any other non-actor (bottles, props, loose items)
};

// Look: where the view rests when walking toward or interacting with something. Shoot: where a shot is aimed.
enum class Purpose { Look, Shoot };

// Height above the target's origin (its feet or base) for `scale` 1. The Look numbers are the ones Walker used before
// the unification; they are estimates, not engine bounds. Shoot has a height only for non-actors, as the fallback when
// an object's mesh bound is unusable. An actor is never shot at a table height (it was 70 for characters and 35 for
// creatures, which landed low: designer, 2026-11-12): it is shot at its torso node (AimUtils::ActorTorsoPoint, todo
// 56), and the height here is 0 for it.
float TargetHeight(TargetKind kind, Purpose purpose);
// TargetHeight scaled by the reference's scale, clamped so a corrupt scale cannot throw the aim away.
float ScaledTargetHeight(TargetKind kind, Purpose purpose, float scale);

// Aim and line of sight start at the engine's own camera position (PlayerCharacter::cameraPos); there is no modelled
// eye height any more (designer, 2026-11-07: use the real value, never a guess). A non-finite camera is unusable, and
// callers then hold the aim and report no line of sight instead of substituting anything.
bool UsableCamera(float x, float y, float z);

// A small object's mesh bound (its world-space centre and radius) replaces the Shoot height table when it is finite,
// the radius is positive and under kMaxObjectBoundRadius, and the centre lies within radius + kBoundSlack of the
// object's origin. A bound that fails this belongs to something else (a stale or parent node) and is ignored.
inline constexpr float kMaxObjectBoundRadius = 300.0f;
inline constexpr float kBoundSlack = 30.0f;
bool UsableObjectBound(float radius, float centreToOrigin);

// Where a shot at an actor is aimed, best first (todo 56). TorsoTarget: the skeleton node the game's body part data
// names as the torso's VATS target, the chest for nearly every body in the game data. TorsoNode: the torso part's own
// node, when the target node is not in the loaded skeleton. MeshBound: the centre of the actor's mesh bound. Origin:
// none of them was usable, so the reference's own position (its feet), which the log marks as worth reporting.
// HeightTable and ObjectBound are the two non-actor sources.
enum class PointSource { TorsoTarget, TorsoNode, MeshBound, ObjectBound, HeightTable, Origin };
const char *PointSourceName(PointSource source);

// A skeleton node counts as a point on the actor when its offset from the actor's origin is finite and no longer than
// kMaxBodyPointOffset times the actor's (clamped) scale. A node further away is not this body's: a stale transform,
// or a skeleton that was not updated since the actor moved.
inline constexpr float kMaxBodyPointOffset = 300.0f;
bool UsableBodyPoint(float dx, float dy, float dz, float scale);

// View pitch that looks from the eye at an offset (dx, dy, dz) away; negative looks up, matching SetPitch.
float PitchTo(float dx, float dy, float dz);

} // namespace AimLogic
