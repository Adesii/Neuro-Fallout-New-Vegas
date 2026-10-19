#include "StanceLogic.hpp"

namespace Stance {

bool IsSneakingFlags(unsigned movementFlags) {
  return (movementFlags & (kMoveFlagSneaking | kMoveFlagSwimming)) == kMoveFlagSneaking;
}

void CrouchVerifier::Begin(bool wasCrouching) {
  m_pending = true;
  m_wasCrouching = wasCrouching;
  m_elapsed = 0.0f;
}

Outcome CrouchVerifier::Update(float dt, bool crouching) {
  if (!m_pending)
    return Outcome::Pending;
  if (crouching != m_wasCrouching) {
    m_pending = false;
    return Outcome::Changed;
  }
  m_elapsed += dt;
  if (m_elapsed >= kTimeoutSeconds) {
    m_pending = false;
    return Outcome::NoEffect;
  }
  return Outcome::Pending;
}

std::string DescribeCrouched(bool crouching) {
  return crouching ? "## Stance\nYou are now crouching and sneaking. Call crouch_toggle again to stand up."
                   : "## Stance\nYou are now standing up. Call crouch_toggle to crouch and sneak again.";
}

std::string DescribeNoEffect(bool crouching) {
  return std::string("## Stance\nThe stance did not change; you are still ") + (crouching ? "crouching" : "standing") +
         ". Crouching may be impossible right now (for example while swimming, in the air or in a menu).";
}

} // namespace Stance
