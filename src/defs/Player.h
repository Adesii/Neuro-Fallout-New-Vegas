#pragma once
// Took this idea from itr since it is a good way to expand the player class without having to modify the original
// struct definitions from xNVSE
//

#include "GameObjects.h"
#include "nvse/NiTypes.h"

namespace Player {
struct PlayerAutoMove {
  UINT8 pad00[0x651];
  bool alwaysRun; // 651
  bool autoMove;  // 652
};

static_assert(offsetof(PlayerAutoMove, alwaysRun) == 0x651);
static_assert(offsetof(PlayerAutoMove, autoMove) == 0x652);

inline void SetAutoMove(PlayerCharacter *player, bool autoMove) {
  if (!player)
    return;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  pAutoMove->autoMove = autoMove;
}

inline bool GetAutoMove(PlayerCharacter *player) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  return pAutoMove->autoMove;
}

inline bool SetAlwaysRun(PlayerCharacter *player, bool alwaysRun) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  pAutoMove->alwaysRun = alwaysRun;
  return true;
}

inline bool GetAlwaysRun(PlayerCharacter *player) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  return pAutoMove->alwaysRun;
}

struct veloctyVector {
  float x;
  float y;
  float z;
};

inline void SetVelocity(PlayerCharacter *player, veloctyVector velocity) {

  auto mover = player->actorMover;
  if (!mover)
    return;
  ThisCall<void>(0x9EA570, mover, &velocity); // PlayerMover::SetSpeedVector
}

inline void ClearMovementFlags(PlayerCharacter *player) {
  auto mover = player->actorMover;
  if (!mover)
    return;
  ThisCall<void>(0x9EA3B0, mover, 0x3F); // PlayerMover::ClearMovementFlag_
}
} // namespace Player
