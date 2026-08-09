#pragma once

#include "GameUI.h"
#include <cstddef>

// Offsets adapted from FalloutNVAccess and verified against char_gen_menu.xml.
namespace CharacterGeneration {

struct MenuData {
  std::byte menu[0x28];
  UINT32 isTagSkills;
  std::byte fields2C[0x58 - 0x2C];
  ListBox<UINT32> actorValues;
};

static_assert(offsetof(MenuData, actorValues) == 0x58);
static_assert(sizeof(MenuData) == 0x88);

inline MenuData *GetMenu() { return *reinterpret_cast<MenuData **>(0x11D920C); }

} // namespace CharacterGeneration
