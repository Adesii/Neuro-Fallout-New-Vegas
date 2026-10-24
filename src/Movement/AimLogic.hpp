#pragma once

// Engine-independent aim maths shared by Walker and Combat (todo 27): angle wrapping, the one height table for "where
// on this thing do I look or shoot", and the stance-aware eye height. AimUtils feeds it engine reads.

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

// Height above the target's origin (its feet or base) for `scale` 1. The numbers are the ones Walker and Combat each
// used before they were unified; they are estimates, not engine bounds. todo 03 / 27 step 1 asks for in-game
// measurements before any of them is changed.
float TargetHeight(TargetKind kind, Purpose purpose);
// TargetHeight scaled by the reference's scale, clamped so a corrupt scale cannot throw the aim away.
float ScaledTargetHeight(TargetKind kind, Purpose purpose, float scale);

// Standing eye height of a scale 1 player (the constant AimUtils always used) and the multiplier for a crouched one.
// kCrouchEyeRatio is 1 (no change) until the crouched eye height is measured in game; no verified engine value is
// available to this project (see todo 27). Set it from the logged numbers.
inline constexpr float kStandingEyeHeight = 100.0f;
inline constexpr float kCrouchEyeRatio = 1.0f;
float EyeHeight(float scale, bool crouching);

// View pitch that looks from the eye at an offset (dx, dy, dz) away; negative looks up, matching SetPitch.
float PitchTo(float dx, float dy, float dz);

} // namespace AimLogic
