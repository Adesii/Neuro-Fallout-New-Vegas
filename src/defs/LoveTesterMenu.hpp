#pragma once

#include "GameUI.h"
#include <cstddef>

// Field offsets and singleton provenance:
// FalloutNVAccess/src/menus/SpecialBookMenuHandler.cpp (MIT), independently matched to love_tester_menu.xml.
namespace LoveTester {

struct Data {
  UINT8 pad00[0x48];
  INT32 currentPage;
  INT32 selectedSpecial;
  UINT8 pad50[0x08];
  INT32 totalPoints;
};

static_assert(offsetof(Data, currentPage) == 0x48);
static_assert(offsetof(Data, selectedSpecial) == 0x4C);
static_assert(offsetof(Data, totalPoints) == 0x58);

inline Menu *GetMenu() { return *reinterpret_cast<Menu **>(0x11DA2C8); }
inline Menu *GetMenuByType() { return InterfaceManager::GetMenuByType(Interface::LoveTester); }
inline Data *GetData() { return reinterpret_cast<Data *>(GetMenu()); }

} // namespace LoveTester
