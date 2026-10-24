#pragma once

#include "GameUI.h"

// Menu singleton pointers of FalloutNV.exe 1.4.0.525 for menus the vendored headers expose no accessor for.
// Provenance: the same addresses appear in JIP-LN-NVSE nvse/GameUI.h (`DialogMenu::Get`, `TraitMenu::Get`),
// FalloutNVAccess src/menus/DialogMenuHandler.cpp and TraitMenuHandler.cpp, and src/itr/internal/GameGlobals.h
// (`g_dialogMenuPtr`). The pointer stays non-null after the menu closes: ask the interface manager whether the menu is
// on top (UIUtils::IsTopMenu), never this pointer alone.
namespace MenuSingletons {

inline constexpr uintptr_t kDialogMenuAddress = 0x11D9510;
inline constexpr uintptr_t kTraitMenuAddress = 0x11DAF74;

inline DialogMenu *Dialog() { return *reinterpret_cast<DialogMenu **>(kDialogMenuAddress); }
inline TraitMenu *Traits() { return *reinterpret_cast<TraitMenu **>(kTraitMenuAddress); }

} // namespace MenuSingletons
