#pragma once

#include "ISingleton.h"
#include "hooks/InputCore.hpp"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

void Hook_DirectInput8Create_Init();

enum { kDeviceType_Keyboard = 1, kDeviceType_Mouse };

enum {
  // first 256 for keyboard, then 8 mouse buttons, then mouse wheel up, wheel down
  kMacro_MouseButtonOffset = 256,
  kMacro_MouseWheelOffset = kMacro_MouseButtonOffset + 8,

  kMaxMacros = kMacro_MouseWheelOffset + 2,
};

class DIHookControl : public ISingleton<DIHookControl> {
public:
  enum {
    // data sources
    kFlag_GameState = 1 << 0,     // input passed to game post-filtering
    kFlag_RawState = 1 << 1,      // user input
    kFlag_InsertedState = 1 << 2, // keydown was inserted by script

    // modifiers
    kFlag_IgnoreDisabled_User = 1 << 3,   // ignore user-disabled keys
    kFlag_IgnoreDisabled_Script = 1 << 4, // ignore script-disabled keys

    kFlag_DefaultBackCompat = kFlag_GameState,
  };

  enum {
    kDisable_User = 1 << 0,
    kDisable_Script = 1 << 1,

    kDisable_All = kDisable_User | kDisable_Script,
  };

  DIHookControl();

  bool IsKeyPressed(UINT32 keycode, UINT32 flags = 0);
  bool IsKeyDisabled(UINT32 keycode);
  bool IsKeyHeld(UINT32 keycode);
  bool IsKeyTapped(UINT32 keycode);

  void SetKeyDisableState(UINT32 keycode, bool bDisable, UINT32 mask);
  // A key stays held while any owner holds it; an owner may hold or release repeatedly without affecting the others.
  void SetKeyHeldState(InputCore::Owner owner, UINT32 keycode, bool bHold);
  void TapKey(UINT32 keycode);

  // Drops every scripted hold, tap and queued buffered event. Called from NeuroSDK::ResetAutomation so a disconnect,
  // load or new game can never leave an injected key or mouse button down.
  void ClearAllScripted();

  // Buffered events go to one queue per device type (kDeviceType_Keyboard / kDeviceType_Mouse); `key` is the
  // DirectInput offset on that device. Nothing calls these yet.
  void BufferedKeyTap(UINT32 deviceType, UINT32 key);
  void BufferedKeyPress(UINT32 deviceType, UINT32 key);
  void BufferedKeyRelease(UINT32 deviceType, UINT32 key);

  // hook interface
  void ProcessKeyboardData(UINT8 *data);
  void ProcessMouseData(DIMOUSESTATE2 *data);
  HRESULT ProcessBufferedData(UINT32 deviceType, IDirectInputDevice8 *device, DWORD dataSize,
                              DIDEVICEOBJECTDATA *outData, DWORD *outDataLen, DWORD flags);

private:
  InputCore::KeyState m_keys[kMaxMacros];

  using BufferedQueue = InputCore::InjectedQueue<DIDEVICEOBJECTDATA>;
  BufferedQueue m_buffered[2]; // indexed by device type - 1

  BufferedQueue *BufferedFor(UINT32 deviceType);
  void QueueBuffered(UINT32 deviceType, UINT32 key, bool press, bool release);
};

class FramerateTracker {
public:
  FramerateTracker();

  void Update(void);

private:
  enum { kFrameTimeHistoryLength = 32 };

  UINT32 m_lastTime;
  float m_lastFrameLength; // frametime in seconds

  float m_frameTimeHistory[kFrameTimeHistoryLength]; // last kFrameTimeHistoryLength frametimes
  UINT32 m_frameTimeHistoryIdx;                      // slot that will be filled on the next update call
  bool m_frameTimeHistoryPrimed;                     // true after history buffer is full

  float m_averageFrameTime; // average of m_frameTimeHistory
};

extern FramerateTracker g_framerateTracker;
