#pragma once

// VATS state for FalloutNV.exe 1.4.0.525. Every address and offset below is cross-checked between two independent
// reference implementations; nothing here is inferred. Runtime semantics (what each mode means to the player) still
// need in-game confirmation, see docs/combat.md.
//
//   Menu singleton 0x11DB0D4, current target ref 0x11F21CC:
//     FalloutNVAccess/src/menus/VATSMenuHandler.cpp (MIT, notice in src/defs/FalloutNVAccess-license.md) and
//     itr-nvse/itr-nvse/internal/GameGlobals.h (see src/itr/itr-license.md).
//   VATS camera mode 0x11F2258 (0x11F2250 + 8): same two files. 0 none, 1 target selection, 2 limb selection,
//     4 playback (FalloutNVAccess VATSMenuHandler.h comment; itr-nvse OnVATSStateHandler.cpp uses 0 and 4).
//   Target list 0x11DB150, VATSTarget / body part layouts: itr-nvse/itr-nvse/internal/layout/VATS.h and
//     JIP-LN-NVSE nvse/GameUI.h (VATSTargetBodyPartData, size 0x3C).
//   VATSMenu fields (0xE0 action points, 0xE4 maximum, 0xF0 loaded ammo, 0xF4 reserve ammo, 0x100 current body part,
//     queue list 0xB0 with item count at +0x1C): FalloutNVAccess VATSMenuHandler.cpp.

#include "GameUI.h"
#include <cstddef>
#include <cstdint>

namespace Vats {

enum Mode : uint32_t { kModeNone = 0, kModeTargetSelection = 1, kModeLimbSelection = 2, kModePlayback = 4 };

template <typename T> struct ListNode {
  T item;
  ListNode<T> *next;
};

struct BodyPart {
  float screenPosX;         // 00
  float screenPosY;         // 04
  float relativePos[3];     // 08
  float pos[3];             // 14
  uint32_t bodyPartId;      // 20
  float percentVisible;     // 24
  float hitChance;          // 28
  uint8_t isOnScreen;       // 2C
  uint8_t chanceCalculated; // 2D
  uint8_t firstTimeShown;   // 2E
  uint8_t needsRecalc;      // 2F
  Tile *percentTile;        // 30, the limb_percent tile; _LimbName and the chance text live on it
  float unk34;              // 34
  uint8_t unk38;            // 38
  uint8_t pad39[3];         // 39
};
static_assert(sizeof(BodyPart) == 0x3C, "VATS body part layout mismatch");
static_assert(offsetof(BodyPart, bodyPartId) == 0x20);
static_assert(offsetof(BodyPart, hitChance) == 0x28);
static_assert(offsetof(BodyPart, percentTile) == 0x30);

struct Target {
  TESObjectREFR *targetRef;       // 00
  uint32_t type;                  // 04
  ListNode<BodyPart *> bodyParts; // 08, the first node is embedded
  uint8_t unk10;                  // 10
  uint8_t pad11[3];               // 11
  uint32_t unk14[3];              // 14
  uint8_t unk20;                  // 20
  uint8_t pad21[3];               // 21
};
static_assert(sizeof(Target) == 0x24, "VATS target layout mismatch");
static_assert(offsetof(Target, bodyParts) == 0x08);

constexpr uint32_t kMenuPointerAddress = 0x11DB0D4;
constexpr uint32_t kCurrentTargetAddress = 0x11F21CC;
constexpr uint32_t kModeAddress = 0x11F2258;
constexpr uint32_t kTargetListAddress = 0x11DB150;

constexpr size_t kOffsetActionPoints = 0xE0;
constexpr size_t kOffsetMaxActionPoints = 0xE4;
constexpr size_t kOffsetCurrentBodyPart = 0x100;

inline Menu *GetMenu() { return *reinterpret_cast<Menu **>(kMenuPointerAddress); }
inline TESObjectREFR *GetCurrentTarget() { return *reinterpret_cast<TESObjectREFR **>(kCurrentTargetAddress); }
inline uint32_t GetMode() { return *reinterpret_cast<uint32_t *>(kModeAddress); }
// The list head is itself the first node; an empty list has a null item.
inline ListNode<Target *> *GetTargetListHead() { return reinterpret_cast<ListNode<Target *> *>(kTargetListAddress); }

inline float ReadMenuFloat(Menu *menu, size_t offset) {
  return *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(menu) + offset);
}
inline BodyPart *GetCurrentBodyPart(Menu *menu) {
  return *reinterpret_cast<BodyPart **>(reinterpret_cast<uint8_t *>(menu) + kOffsetCurrentBodyPart);
}
// VATSMenu::queuedActions is a ListBox at 0xB0 whose itemCount sits at +0x1C (0xCC).
inline int GetQueuedShots(Menu *menu) { return static_cast<VATSMenu *>(menu)->queuedActions.itemCount; }

} // namespace Vats
