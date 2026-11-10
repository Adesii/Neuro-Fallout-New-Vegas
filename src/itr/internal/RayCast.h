// shared Havok raycast payload for TES::PickObject (0x458440)
// layout as in itr-nvse internal/RayCast.h at the revision pinned in tools/reference-manifest.json: the hit normal at
// 0x30 and the byte at 0xAC that itr-nvse treats as "no result" when set (its OnFootContactHandler.cpp)
#pragma once

#include <cstddef>

struct alignas(16) RayCastData {
  float pos0[4];
  float pos1[4];
  UINT8 byte20;
  UINT8 pad21[3];
  UINT8 layerType;
  UINT8 filterFlags;
  UINT16 group;
  UINT32 unk28[2];
  float normal[4];
  float hitFraction;
  UINT32 unk44[15];
  void *cdBody;
  UINT32 unk84[3];
  float vector90[4];
  UINT32 unkA0[3];
  UINT8 budgetSpent;
  UINT8 padAD[3];
};
static_assert(sizeof(RayCastData) == 0xB0, "RayCastData size mismatch");
static_assert(offsetof(RayCastData, normal) == 0x30, "RayCastData normal offset mismatch");
static_assert(offsetof(RayCastData, hitFraction) == 0x40, "RayCastData hit fraction offset mismatch");
static_assert(offsetof(RayCastData, cdBody) == 0x80, "RayCastData collidable offset mismatch");
static_assert(offsetof(RayCastData, budgetSpent) == 0xAC, "RayCastData budget offset mismatch");

constexpr float kHavokScale = 0.1428571f;
