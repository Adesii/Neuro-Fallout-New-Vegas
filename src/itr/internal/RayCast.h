// shared Havok raycast payload for TES::PickObject (0x458440)
#pragma once

struct alignas(16) RayCastData {
  float pos0[4];
  float pos1[4];
  UINT8 byte20;
  UINT8 pad21[3];
  UINT8 layerType;
  UINT8 filterFlags;
  UINT16 group;
  UINT32 unk28[6];
  float hitFraction;
  UINT32 unk44[15];
  void *cdBody;
  UINT32 unk84[3];
  float vector90[4];
  UINT32 unkA0[3];
  UINT8 byteAC;
  UINT8 padAD[3];
};
static_assert(sizeof(RayCastData) == 0xB0, "RayCastData size mismatch");

constexpr float kHavokScale = 0.1428571f;
