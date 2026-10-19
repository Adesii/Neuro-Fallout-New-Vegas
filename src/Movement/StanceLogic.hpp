#pragma once

// Engine-independent crouch bookkeeping. StanceHandler taps the Sneak control and feeds this the sneak state it reads
// back from the engine, so the toggle is reported from what actually happened and not from the tap.

#include <string>

namespace Stance {

// Movement flag bits of the player's ActorMover (FalloutNV.exe 1.4.0.525): JIP-LN-NVSE `Actor::IsSneaking` and
// itr-nvse `CrouchCommands` both test the sneak bit 0x400 with the swimming bit 0x800 masked out. The vendored
// JG header comment ("9: IsSneaking") disagrees with them and is not used.
inline constexpr unsigned kMoveFlagSneaking = 0x400;
inline constexpr unsigned kMoveFlagSwimming = 0x800;

bool IsSneakingFlags(unsigned movementFlags);

enum class Outcome { Pending, Changed, NoEffect };

// Waits for the sneak state to flip after a tap. Idle until Begin(); Update() returns Pending while waiting.
class CrouchVerifier {
public:
  static constexpr float kTimeoutSeconds = 1.5f;

  void Begin(bool wasCrouching);
  bool IsPending() const { return m_pending; }
  void Cancel() { m_pending = false; }
  // `crouching` is the state read this frame. Resolves at most once per Begin().
  Outcome Update(float dt, bool crouching);

private:
  bool m_pending = false;
  bool m_wasCrouching = false;
  float m_elapsed = 0.0f;
};

std::string DescribeCrouched(bool crouching);
std::string DescribeNoEffect(bool crouching);

} // namespace Stance
