#pragma once
// Took this idea from itr since it is a good way to expand the player class without having to modify the original
// struct definitions from xNVSE
//

#include "GameObjects.h"
#include "common/ITypes.h"
struct PlayerAutoMove {
  UInt8 pad00[0x651];
  bool alwaysRun; // 651
  bool autoMove;  // 652
};

static_assert(offsetof(PlayerAutoMove, alwaysRun) == 0x651);
static_assert(offsetof(PlayerAutoMove, autoMove) == 0x652);

inline void SetPlayerAutoMove(PlayerCharacter *player, bool autoMove) {
  if (!player)
    return;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  pAutoMove->autoMove = autoMove;
}

inline bool GetPlayerAutoMove(PlayerCharacter *player) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  return pAutoMove->autoMove;
}

inline bool SetPlayerAlwaysRun(PlayerCharacter *player, bool alwaysRun) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  pAutoMove->alwaysRun = alwaysRun;
  return true;
}

inline bool GetPlayerAlwaysRun(PlayerCharacter *player) {
  if (!player)
    return false;
  PlayerAutoMove *pAutoMove = reinterpret_cast<PlayerAutoMove *>(player);
  return pAutoMove->alwaysRun;
}
