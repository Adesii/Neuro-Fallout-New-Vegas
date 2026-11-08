#include "Debug/WalkerOverlay.hpp"

#include "Bethesda/TESMain.hpp"
#include "GameObjects.h"
#include "Gamebryo/NiRenderer.hpp"
#include "SafeWrite/SafeWrite.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "netimmerse.h"
#include "nvse/GameAPI.h"
#include "nvse/ParamInfos.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

// Engine facts used here, each from two sources that agree:
// - The renderer singleton at 0x11F4748: JG `internal/Game/Gamebryo/NiRenderer.cpp` (NiRenderer::GetRenderer) and
//   JIP-LN-NVSE `internal/netimmerse.h` (NiDX9Renderer::GetSingleton). It is an NiDX9Renderer in the game.
// - NiDX9Renderer::device (IDirect3DDevice9 *) at 0x288: JG `internal/netimmerse.h` (static_assert on the class size
//   0xB80) and JIP-LN-NVSE `internal/netimmerse.h`, both annotated 288.
// - The world camera, TESMain::GetWorldRootCamera() (the world scene graph at 0x11DEB7C, its spCamera), as
//   src/utils/AimUtils.cpp already reads it. NiCamera::m_aafWorldToCam at 0x9C and m_kPort (NiViewport l, r, t, b) at
//   0x100: JG `internal/netimmerse.h` (static_assert sizeof(NiCamera) == 0x114) and JIP-LN-NVSE, whose
//   NiCamera::WorldToScreen (`internal/netimmerse.cpp`) reads the matrix at +0x9C and the viewport at +0x9C+0x64.
//   JIP reads the matrix whenever a script asks; JG copies it from a hook only while the camera's FOV is the player's
//   world FOV, which suggests the matrix is sometimes rewritten during a frame. Read at Present here.
// - Field of view. With the raw matrix the overlay slid while turning and drifted when getting closer (2026-11-08
//   playtest): the camera still holds the last FOV the game set on it. BSSceneGraph::SetCameraFOV is 0xC52020 (JG
//   `Bethesda/BSSceneGraph.cpp`, JIP and yUI SceneGraph); JG's WorldToScreen hook inside it only keeps the matrix when
//   the FOV is PlayerCharacter::worldFOV. The overlay scales the frustum by tan(worldFOV / 2) / tan(fCurrentFOV / 2),
//   the scene graph's last FOV. Of the corrections tried in game that one stopped the sliding (the designer,
//   2026-11-08); the raw matrix and a frustum-width-to-worldFOV rescale did not. NiCamera::m_kViewFrustum (NiFrustum)
//   at 0xDC (JG `internal/netimmerse.h`, JIP `frustum // 0DC`); PlayerCharacter::worldFOV at 0x670 and firstPersonFOV
//   after it (JG `nvse/GameObjects.h`, itr-nvse `internal/layout/Player.h` static_asserts 0x670 and 0x674);
//   BSSceneGraph::fCurrentFOV (JG `internal/netimmerse.h`, JIP `cameraFOV // BC`).
// - IDirect3DDevice9::Present is slot 17 of the device's vtable: the declaration order in the Windows SDK d3d9.h
//   (IUnknown's 3, then TestCooperativeLevel ... Reset at 16, Present at 17). Patched in the vtable, which every
//   device of the process shares, so the hook only draws for the game's own device.
// The projection maths are this project's own (src/Debug/OverlayMath.cpp); no reference code is copied.

namespace WalkerOverlay {
namespace {

constexpr size_t kPresentSlot = 17;
constexpr float kReferenceHeight = 1080.0f;

using PresentFn = HRESULT(__stdcall *)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);

bool g_enabled = false;
LogLevel g_logLevel = LogLevel::Off;
bool g_hookInstalled = false;
bool g_hookFailed = false;
// One-time lines at LogLevel::Overlay that tell apart "Present never reached the hook", "no usable camera", "nothing
// projected" and "drawn". Reset whenever that level is chosen, so they can be asked for again.
bool g_loggedFirstPresent = false;
bool g_loggedNoCamera = false;
bool g_loggedEmptyBatch = false;
bool g_loggedFirstDraw = false;
IDirect3DDevice9 *g_device = nullptr;
PresentFn g_originalPresent = nullptr;
DWORD g_mainThread = 0;

// The last camera numbers written to the log (LogLevel::Overlay); a new line when any of them moves.
struct CameraLog {
  bool logged = false;
  float frustumFov = 0.0f;
  float worldFov = 0.0f;
  float sceneFov = 0.0f;
};
CameraLog g_cameraLog;

bool Moved(float a, float b) { return !(std::abs(a - b) <= 0.5f); }

bool LogsOverlay() { return g_logLevel == LogLevel::Overlay; }

std::mutex g_viewMutex;
bool g_hasView = false;
RouteDebug::View g_view;
Overlay::Batch g_batch;

IDirect3DDevice9 *GameDevice() {
  auto *renderer = static_cast<NiDX9Renderer *>(NiRenderer::GetRenderer());
  return renderer ? renderer->device : nullptr;
}

bool ReadCamera(float width, float height, Overlay::Camera &out) {
  const SceneGraph *sceneGraph = TESMain::GetWorldSceneGraph();
  const NiCamera *camera = sceneGraph ? TESMain::GetWorldRootCamera() : nullptr;
  if (!camera)
    return false;
  static_assert(sizeof(out.worldToClip) == sizeof(camera->m_aafWorldToCam), "4x4 float matrix");
  std::memcpy(out.worldToClip, camera->m_aafWorldToCam, sizeof(out.worldToClip));
  out.left = camera->m_kPort.l;
  out.right = camera->m_kPort.r;
  out.top = camera->m_kPort.t;
  out.bottom = camera->m_kPort.b;
  out.width = width;
  out.height = height;
  if (!Overlay::UsableCamera(out))
    return false;

  const NiFrustum &f = camera->m_kViewFrustum;
  const Overlay::Frustum frustum = {f.m_fLeft, f.m_fRight, f.m_fTop, f.m_fBottom};
  const PlayerCharacter *player = PlayerCharacter::GetSingleton();
  const float worldFov = player ? player->worldFOV : 0.0f;
  const float firstPersonFov = player ? player->firstPersonFOV : 0.0f;
  const float sceneFov = sceneGraph->fCurrentFOV;
  const float scale = Overlay::ScaleBetweenFovs(sceneFov, worldFov);
  if (scale > 0.0f)
    Overlay::ScaleFieldOfView(out, frustum, scale);

  if (LogsOverlay()) {
    const float frustumFov = Overlay::FrustumAngleDegrees(frustum.left, frustum.right);
    if (!g_cameraLog.logged || Moved(frustumFov, g_cameraLog.frustumFov) || Moved(worldFov, g_cameraLog.worldFov) ||
        Moved(sceneFov, g_cameraLog.sceneFov)) {
      g_cameraLog = {true, frustumFov, worldFov, sceneFov};
      _MESSAGE("Walker debug camera: frustum l %.4f r %.4f t %.4f b %.4f near %.1f far %.0f (horizontal %.1f deg, "
               "vertical %.1f deg); back buffer aspect %.3f; worldFOV %.1f, firstPersonFOV %.1f, scene FOV %.1f; "
               "scale %.4f",
               frustum.left, frustum.right, frustum.top, frustum.bottom, f.m_fNear, f.m_fFar, frustumFov,
               Overlay::FrustumAngleDegrees(frustum.bottom, frustum.top), width / height, worldFov, firstPersonFov,
               sceneFov, scale);
    }
  }
  return Overlay::UsableCamera(out);
}

void SetOverlayState(IDirect3DDevice9 *device) {
  device->SetVertexShader(nullptr);
  device->SetPixelShader(nullptr);
  device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  device->SetTexture(0, nullptr);
  device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
  device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  device->SetRenderState(D3DRS_FOGENABLE, FALSE);
  device->SetRenderState(D3DRS_CLIPPING, TRUE);
  device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
  device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
  device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
  device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
  device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

// Draws the overlay onto the back buffer just before the frame is shown. No Direct3D resource outlives the call (the
// state block and surface references are released at once), so a device Reset (alt-tab) has nothing to recreate.
void Draw(IDirect3DDevice9 *device) {
  {
    std::lock_guard lock(g_viewMutex);
    if (!g_hasView)
      return;
  }

  IDirect3DSurface9 *backBuffer = nullptr;
  if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
    return;
  D3DSURFACE_DESC desc{};
  backBuffer->GetDesc(&desc);
  Overlay::Camera camera;
  if (!ReadCamera(static_cast<float>(desc.Width), static_cast<float>(desc.Height), camera)) {
    if (LogsOverlay() && !g_loggedNoCamera) {
      g_loggedNoCamera = true;
      _WARNING("Walker debug overlay: no usable world camera at Present (%ux%u back buffer); nothing drawn.",
               desc.Width, desc.Height);
    }
    backBuffer->Release();
    return;
  }

  g_batch.Clear();
  RouteDebug::Point3 player;
  {
    std::lock_guard lock(g_viewMutex);
    RouteDebug::BuildOverlay(g_view, camera, camera.height / kReferenceHeight, g_batch);
    player = g_view.player;
  }
  if (g_batch.Triangles() == 0) {
    // Normal while the route is behind the view; logged once in case it is the matrix (w not in front of the camera).
    if (LogsOverlay() && !g_loggedEmptyBatch) {
      g_loggedEmptyBatch = true;
      const auto &w = camera.worldToClip[3];
      _MESSAGE("Walker debug overlay: a route exists but nothing projected on screen (matrix row 3: %.4f %.4f %.4f "
               "%.4f; clip w at the player %.2f, needs >= %.1f; viewport l %.3f r %.3f t %.3f b %.3f)",
               w[0], w[1], w[2], w[3], w[0] * player.x + w[1] * player.y + w[2] * player.z + w[3], Overlay::kNearW,
               camera.left, camera.right, camera.top, camera.bottom);
    }
    backBuffer->Release();
    return;
  }

  if (LogsOverlay() && !g_loggedFirstDraw) {
    g_loggedFirstDraw = true;
    _MESSAGE("Walker debug overlay: first frame drawn (%ux%u back buffer, Present thread %lu, main loop thread %lu, "
             "viewport l %.3f r %.3f t %.3f b %.3f)",
             desc.Width, desc.Height, GetCurrentThreadId(), g_mainThread, camera.left, camera.right, camera.top,
             camera.bottom);
  }

  IDirect3DStateBlock9 *saved = nullptr;
  device->CreateStateBlock(D3DSBT_ALL, &saved);
  IDirect3DSurface9 *previousTarget = nullptr;
  device->GetRenderTarget(0, &previousTarget);
  device->SetRenderTarget(0, backBuffer);
  if (SUCCEEDED(device->BeginScene())) {
    SetOverlayState(device);
    const auto &vertices = g_batch.Vertices();
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(g_batch.Triangles()), vertices.data(),
                            sizeof(Overlay::Vertex));
    device->EndScene();
  }
  if (previousTarget) {
    device->SetRenderTarget(0, previousTarget);
    previousTarget->Release();
  }
  if (saved) {
    saved->Apply(); // after the render target: setting one resets the viewport, which the block restores
    saved->Release();
  }
  backBuffer->Release();
}

HRESULT __stdcall HookedPresent(IDirect3DDevice9 *device, const RECT *source, const RECT *destination, HWND window,
                                const RGNDATA *dirty) {
  if (g_enabled && device == g_device) {
    if (LogsOverlay() && !g_loggedFirstPresent) {
      g_loggedFirstPresent = true;
      _MESSAGE("Walker debug overlay: Present reached the hook on the game's device (thread %lu).",
               GetCurrentThreadId());
    }
    Draw(device);
  }
  return g_originalPresent(device, source, destination, window, dirty);
}

void InstallHook() {
  IDirect3DDevice9 *device = GameDevice();
  if (!device)
    return; // the renderer is not up yet; try again next frame
  auto **vtable = *reinterpret_cast<void ***>(device);
  if (!vtable || !vtable[kPresentSlot]) {
    g_hookFailed = true;
    _ERROR("Walker debug overlay: the Direct3D device has no Present entry; overlay disabled.");
    return;
  }
  D3DDEVICE_CREATION_PARAMETERS creation{};
  if (SUCCEEDED(device->GetCreationParameters(&creation)) && (creation.BehaviorFlags & D3DCREATE_PUREDEVICE))
    _WARNING("Walker debug overlay: the device is a pure device; saving its state may fail.");
  g_device = device;
  g_originalPresent = reinterpret_cast<PresentFn>(vtable[kPresentSlot]);
  HookUtils::SafeWrite32(reinterpret_cast<uintptr_t>(&vtable[kPresentSlot]),
                         reinterpret_cast<uint32_t>(&HookedPresent));
  g_hookInstalled = true;
  g_mainThread = GetCurrentThreadId();
  _MESSAGE("Walker debug overlay: Present hook installed on device %p (original Present %p).",
           static_cast<void *>(device), reinterpret_cast<void *>(g_originalPresent));
}

} // namespace

bool Enabled() { return g_enabled; }

LogLevel GetLogLevel() { return g_logLevel; }

void SetEnabled(bool enabled) {
  if (enabled == g_enabled)
    return;
  g_enabled = enabled;
  Clear();
  _MESSAGE("Walker debug mode %s.", enabled ? "on" : "off");
  if (enabled && g_hookFailed)
    _WARNING("Walker debug overlay cannot draw (hook failed earlier); route lines go to the log only.");
}

void Publish(const RouteDebug::View &view) {
  std::lock_guard lock(g_viewMutex);
  g_view = view;
  g_hasView = true;
}

void Clear() {
  std::lock_guard lock(g_viewMutex);
  g_view = {};
  g_hasView = false;
}

void Update() {
  if (g_enabled && !g_hookInstalled && !g_hookFailed)
    InstallHook();
}

// From the console: `NWalkerDebug` toggles, `NWalkerDebug 1` / `NWalkerDebug 0` set it, and an optional second number
// sets the log level (`NWalkerDebug 1 0` none, the default, `1` route changes, `2` also camera and overlay
// diagnostics); without it the level stays as it was. Returns the new state. The macro's third argument is
// refRequired (DEFINE_CMD_FULL in nvse/CommandTable.h), so false: no reference needed.
DEFINE_NEURO_COMMAND_PLUGIN(WalkerDebug, "Walker debug overlay: [on 1/off 0, none toggles] [log level 0-2]", false,
                            kParams_TwoOptionalInts);

bool Cmd_NWalkerDebug_Execute(COMMAND_ARGS) {
  int value = -1;
  int level = -1;
  if (!ExtractArgsEx(EXTRACT_ARGS_EX, &value, &level))
    return true;
  if (level >= 0) {
    g_logLevel = static_cast<LogLevel>(std::min(level, static_cast<int>(LogLevel::Overlay)));
    if (g_logLevel == LogLevel::Overlay) {
      g_loggedFirstPresent = g_loggedNoCamera = g_loggedEmptyBatch = g_loggedFirstDraw = false;
      g_cameraLog = {};
    }
  }
  SetEnabled(value < 0 ? !g_enabled : value != 0);
  *result = g_enabled ? 1.0 : 0.0;
  static constexpr const char *kLevelNames[] = {"off", "route changes", "route changes, camera and overlay"};
  Console_Print("Walker debug %s, log level %d (%s).", g_enabled ? "on" : "off", static_cast<int>(g_logLevel),
                kLevelNames[static_cast<int>(g_logLevel)]);
  if (g_enabled)
    Console_Print("Cyan: navmesh path, orange: path to the navmesh point next to the target, red: straight line (no "
                  "path), green: point steered at, magenta: Wander spot, blue: walked trail.");
  return true;
}

void RegisterCommands(NVSEInterface *nvse) { REG_CMD(nvse, WalkerDebug); }

} // namespace WalkerOverlay
