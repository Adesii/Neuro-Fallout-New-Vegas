#pragma once

// Engine-free maths for the debug overlay (todo 43): projecting world points through a camera's world-to-clip matrix,
// clipping segments that run behind the camera, and building a batch of screen-space triangles (thick lines, boxes,
// digits). src/Debug/WalkerOverlay.cpp fills `Camera` from the engine and hands the batch to Direct3D 9; everything
// here builds and is tested on the host.

#include <cstdint>
#include <vector>

namespace Overlay {

struct Point3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// A world-to-clip matrix in the row layout of Gamebryo's NiCamera::m_aafWorldToCam (row r times (x, y, z, 1) is clip
// component r; row 3 is w), the camera's viewport (NiViewport l, r, t, b) and the render target size in pixels.
struct Camera {
  float worldToClip[4][4] = {};
  float left = 0.0f;
  float right = 1.0f;
  float top = 1.0f;
  float bottom = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Layout of one D3DFVF_XYZRHW | D3DFVF_DIFFUSE vertex: pre-transformed screen position and an ARGB colour.
struct Vertex {
  float x;
  float y;
  float z;
  float rhw;
  uint32_t color;
};
static_assert(sizeof(Vertex) == 20, "matches the D3DFVF_XYZRHW | D3DFVF_DIFFUSE stride");

struct ScreenPoint {
  float x = 0.0f;
  float y = 0.0f;
};

// Points with w below this are treated as behind the camera.
inline constexpr float kNearW = 1.0f;

// Whether the camera is usable at all (finite matrix, non-empty viewport and target).
bool UsableCamera(const Camera &camera);

// Pixel position of a world point. False when it lies behind the camera (w < kNearW); the point may still be off
// screen when true.
bool Project(const Camera &camera, const Point3 &world, ScreenPoint &out);

// The visible part of a world segment in pixels: clipped against w >= kNearW before the divide, so a segment with one
// end behind the camera keeps its visible half instead of flipping across the screen, then clipped to the screen
// enlarged by `margin` pixels on every side. False when nothing of it is left.
bool ProjectSegment(const Camera &camera, const Point3 &a, const Point3 &b, ScreenPoint &outA, ScreenPoint &outB,
                    float margin = 64.0f);

// --- Field of view correction -----------------------------------------------------------------------------------

// A Gamebryo NiFrustum's sides: tangents of the half angles, on the plane one unit in front of the camera.
struct Frustum {
  float left = 0.0f;
  float right = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
};

// Which field of view the overlay projects with. The world camera still holds the last FOV the game set on it when
// the frame is shown, which in play was not the world's (the overlay slid while turning, 2026-11-08 playtest; the
// first-person pass sets its own FOV on the same camera, as JG's WorldToScreen hook guards against). The default
// rescales the camera's frustum to the player's world FOV.
enum class FovCorrection {
  None = 0,       // the camera's matrix as it is at Present
  WorldFov = 1,   // frustum width rescaled so the horizontal FOV is the player's worldFOV
  SceneRatio = 2, // scaled by tan(worldFOV / 2) / tan(scene graph's current FOV / 2)
};
inline constexpr int kFovCorrectionCount = 3;
const char *FovCorrectionName(FovCorrection mode);

// Full angle in degrees spanned by two frustum sides (left/right or bottom/top). NaN when not a usable frustum.
float FrustumAngleDegrees(float low, float high);

// The factor that widens `frustum` to a horizontal field of view of `fovDegrees`. 0 when either is unusable.
float ScaleToHorizontalFov(const Frustum &frustum, float fovDegrees);

// tan(to / 2) / tan(from / 2): the factor that turns a frustum built for `fromDegrees` into one for `toDegrees`.
// 0 when either is outside (0, 180).
float ScaleBetweenFovs(float fromDegrees, float toDegrees);

// Rewrites the camera's x and y clip rows as if its frustum, `frustum`, had every side multiplied by `scale` (scale >
// 1 widens the view). Uses clip x = (2 * x_cam - (r + l) * w) / (r - l), the perspective form the matrix is read in;
// for a symmetric frustum this is the x and y rows divided by `scale`. Does nothing for a non-positive or non-finite
// scale or an unusable frustum.
void ScaleFieldOfView(Camera &camera, const Frustum &frustum, float scale);

// ARGB colour with the alpha scaled by `alpha` in [0, 1].
uint32_t Fade(uint32_t color, float alpha);

// Screen-space triangles for one DrawPrimitiveUP(D3DPT_TRIANGLELIST) call.
class Batch {
public:
  void Clear() { m_vertices.clear(); }
  const std::vector<Vertex> &Vertices() const { return m_vertices; }
  size_t Triangles() const { return m_vertices.size() / 3; }

  // A line `width` pixels thick (a quad). Degenerate (zero-length) lines add nothing.
  void Line(ScreenPoint a, ScreenPoint b, float width, uint32_t color);
  // A filled square of half-size `half` around `centre`.
  void Box(ScreenPoint centre, float half, uint32_t color);
  // A square outline of half-size `half`, `width` pixels thick.
  void Frame(ScreenPoint centre, float half, float width, uint32_t color);
  // A non-negative integer in seven-segment digits, `height` pixels tall, with its top-left corner at `topLeft`.
  // Returns the width drawn.
  float Number(ScreenPoint topLeft, float height, unsigned value, uint32_t color);

  // World-space helpers: nothing is added for parts behind the camera.
  void WorldLine(const Camera &camera, const Point3 &a, const Point3 &b, float width, uint32_t color);
  bool WorldBox(const Camera &camera, const Point3 &p, float half, uint32_t color, ScreenPoint *where = nullptr);

private:
  void Triangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, uint32_t color);
  std::vector<Vertex> m_vertices;
};

// Seven-segment mask of a digit: bit 0 top, 1 top right, 2 bottom right, 3 bottom, 4 bottom left, 5 top left,
// 6 middle. 0 for anything that is not 0-9.
uint8_t DigitSegments(int digit);

} // namespace Overlay
