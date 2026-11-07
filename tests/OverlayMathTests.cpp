#include "Debug/OverlayMath.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <limits>

using namespace Overlay;

namespace {

// A camera at the origin looking along +y with z up: clip x = world x, clip y = world z, w = world y (a 90 degree
// field of view both ways), on a 1000 x 500 target with Gamebryo's full viewport (0, 1, 1, 0).
Camera TestCamera() {
  Camera camera;
  camera.worldToClip[0][0] = 1.0f; // x
  camera.worldToClip[1][2] = 1.0f; // y from world z
  camera.worldToClip[2][1] = 1.0f; // depth
  camera.worldToClip[3][1] = 1.0f; // w = world y
  camera.left = 0.0f;
  camera.right = 1.0f;
  camera.top = 1.0f;
  camera.bottom = 0.0f;
  camera.width = 1000.0f;
  camera.height = 500.0f;
  return camera;
}

bool Near(float a, float b, float tolerance = 0.01f) { return std::abs(a - b) <= tolerance; }

} // namespace

TEST("overlay: a point straight ahead projects to the centre, the view's edges to the screen's edges") {
  const Camera camera = TestCamera();
  ScreenPoint s;
  CHECK(Project(camera, {0.0f, 10.0f, 0.0f}, s));
  CHECK(Near(s.x, 500.0f) && Near(s.y, 250.0f));
  CHECK(Project(camera, {10.0f, 10.0f, 0.0f}, s));
  CHECK(Near(s.x, 1000.0f));
  CHECK(Project(camera, {0.0f, 10.0f, 10.0f}, s)); // up in the world is up on screen (y = 0 at the top)
  CHECK(Near(s.y, 0.0f));
  CHECK(Project(camera, {-5.0f, 10.0f, -5.0f}, s));
  CHECK(Near(s.x, 250.0f) && Near(s.y, 375.0f));
}

TEST("overlay: a point behind the camera does not project") {
  const Camera camera = TestCamera();
  ScreenPoint s;
  CHECK(!Project(camera, {0.0f, -10.0f, 0.0f}, s));
  CHECK(!Project(camera, {0.0f, 0.0f, 0.0f}, s));
}

TEST("overlay: a segment with one end behind the camera keeps its visible part on the right side") {
  const Camera camera = TestCamera();
  ScreenPoint a;
  ScreenPoint b;
  // From behind the camera on the right to ahead on the right: the visible end is at x/w = 0.5.
  CHECK(ProjectSegment(camera, {5.0f, -10.0f, 0.0f}, {5.0f, 10.0f, 0.0f}, a, b));
  CHECK(Near(b.x, 750.0f) && Near(b.y, 250.0f));
  // The clipped end runs off the right edge (the near plane is close), never flipped to the left half.
  CHECK(a.x > 750.0f);
  CHECK(a.x <= 1000.0f + 64.0f + 0.01f);
  CHECK(std::isfinite(a.x) && std::isfinite(a.y));
}

TEST("overlay: a segment entirely behind the camera or off screen is dropped") {
  const Camera camera = TestCamera();
  ScreenPoint a;
  ScreenPoint b;
  CHECK(!ProjectSegment(camera, {0.0f, -10.0f, 0.0f}, {5.0f, -20.0f, 0.0f}, a, b));
  // Far to the right of the view, beyond the margin.
  CHECK(!ProjectSegment(camera, {100.0f, 10.0f, 0.0f}, {200.0f, 10.0f, 0.0f}, a, b));
}

TEST("overlay: a segment crossing the screen is clipped to the margin rectangle") {
  const Camera camera = TestCamera();
  ScreenPoint a;
  ScreenPoint b;
  CHECK(ProjectSegment(camera, {-100.0f, 10.0f, 0.0f}, {100.0f, 10.0f, 0.0f}, a, b, 10.0f));
  CHECK(Near(a.x, -10.0f) && Near(b.x, 1010.0f));
  CHECK(Near(a.y, 250.0f) && Near(b.y, 250.0f));
}

TEST("overlay: the viewport fraction places the camera's picture on the target") {
  Camera camera = TestCamera();
  camera.left = 0.5f; // the right half of the target
  ScreenPoint s;
  CHECK(Project(camera, {0.0f, 10.0f, 0.0f}, s));
  CHECK(Near(s.x, 750.0f));
}

TEST("overlay: an unusable camera is refused") {
  Camera camera = TestCamera();
  CHECK(UsableCamera(camera));
  camera.worldToClip[0][0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(!UsableCamera(camera));
  camera = TestCamera();
  camera.width = 0.0f;
  CHECK(!UsableCamera(camera));
  camera = TestCamera();
  camera.right = camera.left;
  CHECK(!UsableCamera(camera));
}

TEST("overlay: lines and boxes are two triangles each, degenerate lines nothing") {
  Batch batch;
  batch.Line({0.0f, 0.0f}, {10.0f, 0.0f}, 2.0f, 0xFFFFFFFF);
  CHECK_EQ(batch.Triangles(), size_t{2});
  // The quad is `width` thick around the line.
  float minY = 1e9f;
  float maxY = -1e9f;
  for (const Vertex &v : batch.Vertices()) {
    minY = std::min(minY, v.y);
    maxY = std::max(maxY, v.y);
    CHECK_EQ(v.rhw, 1.0f);
  }
  CHECK(Near(minY, -1.0f) && Near(maxY, 1.0f));
  batch.Line({5.0f, 5.0f}, {5.0f, 5.0f}, 2.0f, 0xFFFFFFFF);
  CHECK_EQ(batch.Triangles(), size_t{2});
  batch.Box({0.0f, 0.0f}, 3.0f, 0xFF00FF00);
  CHECK_EQ(batch.Triangles(), size_t{4});
}

TEST("overlay: digits use the seven-segment masks") {
  CHECK_EQ(DigitSegments(8), uint8_t{0x7F});
  CHECK_EQ(DigitSegments(1), uint8_t{0x06});
  CHECK_EQ(DigitSegments(10), uint8_t{0});
  Batch batch;
  const float width = batch.Number({0.0f, 0.0f}, 10.0f, 18, 0xFFFFFFFF);
  // "1" has 2 segments and "8" has 7, each a two-triangle line.
  CHECK_EQ(batch.Triangles(), size_t{18});
  CHECK(width > 10.0f);
  batch.Clear();
  batch.Number({0.0f, 0.0f}, 10.0f, 0, 0xFFFFFFFF);
  CHECK_EQ(batch.Triangles(), size_t{12});
}

TEST("overlay: Fade scales only the alpha") {
  CHECK_EQ(Fade(0xFF123456, 0.5f), uint32_t{0x80123456});
  CHECK_EQ(Fade(0xFF123456, 2.0f), uint32_t{0xFF123456});
  CHECK_EQ(Fade(0xFF123456, -1.0f), uint32_t{0x00123456});
}
