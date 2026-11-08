#include "Debug/OverlayMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Overlay {
namespace {

struct Clip4 {
  float x;
  float y;
  float z;
  float w;
};

Clip4 ToClip(const Camera &camera, const Point3 &p) {
  const auto &m = camera.worldToClip;
  return {
      m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3], m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
      m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3], m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3]};
}

Clip4 Lerp(const Clip4 &a, const Clip4 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

// Clip coordinates (w >= kNearW) to pixels. The viewport is a fraction of the render target with bottom 0 and top 1
// (Gamebryo's convention), so the vertical axis is flipped for screen pixels that grow downwards.
ScreenPoint ToScreen(const Camera &camera, const Clip4 &clip) {
  const float ndcX = clip.x / clip.w;
  const float ndcY = clip.y / clip.w;
  const float fractionX = ndcX * (camera.right - camera.left) * 0.5f + (camera.right + camera.left) * 0.5f;
  const float fractionY = ndcY * (camera.top - camera.bottom) * 0.5f + (camera.top + camera.bottom) * 0.5f;
  return {fractionX * camera.width, (1.0f - fractionY) * camera.height};
}

// Liang-Barsky clip of a 2D segment against a rectangle.
bool ClipToRect(ScreenPoint &a, ScreenPoint &b, float minX, float minY, float maxX, float maxY) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  float t0 = 0.0f;
  float t1 = 1.0f;
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {a.x - minX, maxX - a.x, a.y - minY, maxY - a.y};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0f) {
      if (q[i] < 0.0f)
        return false;
      continue;
    }
    const float t = q[i] / p[i];
    if (p[i] < 0.0f)
      t0 = std::max(t0, t);
    else
      t1 = std::min(t1, t);
    if (t0 > t1)
      return false;
  }
  const ScreenPoint start = a;
  a = {start.x + dx * t0, start.y + dy * t0};
  b = {start.x + dx * t1, start.y + dy * t1};
  return true;
}

bool Finite(float value) { return std::isfinite(value); }

constexpr float kDegreesPerRadian = 57.29577951308232f;

} // namespace

bool UsableCamera(const Camera &camera) {
  for (const auto &row : camera.worldToClip)
    for (float value : row)
      if (!Finite(value))
        return false;
  return Finite(camera.left) && Finite(camera.right) && Finite(camera.top) && Finite(camera.bottom) &&
         camera.right != camera.left && camera.top != camera.bottom && camera.width > 0.0f && camera.height > 0.0f;
}

bool Project(const Camera &camera, const Point3 &world, ScreenPoint &out) {
  const Clip4 clip = ToClip(camera, world);
  if (!(clip.w >= kNearW))
    return false;
  out = ToScreen(camera, clip);
  return Finite(out.x) && Finite(out.y);
}

bool ProjectSegment(const Camera &camera, const Point3 &a, const Point3 &b, ScreenPoint &outA, ScreenPoint &outB,
                    float margin) {
  Clip4 ca = ToClip(camera, a);
  Clip4 cb = ToClip(camera, b);
  const bool aIn = ca.w >= kNearW;
  const bool bIn = cb.w >= kNearW;
  if (!aIn && !bIn)
    return false;
  if (!aIn)
    ca = Lerp(ca, cb, (kNearW - ca.w) / (cb.w - ca.w));
  else if (!bIn)
    cb = Lerp(ca, cb, (kNearW - ca.w) / (cb.w - ca.w));
  outA = ToScreen(camera, ca);
  outB = ToScreen(camera, cb);
  if (!Finite(outA.x) || !Finite(outA.y) || !Finite(outB.x) || !Finite(outB.y))
    return false;
  return ClipToRect(outA, outB, -margin, -margin, camera.width + margin, camera.height + margin);
}

const char *FovCorrectionName(FovCorrection mode) {
  switch (mode) {
  case FovCorrection::None:
    return "none (camera matrix as it is)";
  case FovCorrection::WorldFov:
    return "world FOV (frustum width from worldFOV)";
  case FovCorrection::SceneRatio:
    return "scene ratio (worldFOV against the scene graph's current FOV)";
  }
  return "?";
}

float FrustumAngleDegrees(float low, float high) {
  const float half = (high - low) * 0.5f;
  if (!Finite(half) || !(half > 0.0f))
    return std::numeric_limits<float>::quiet_NaN();
  return 2.0f * std::atan(half) * kDegreesPerRadian;
}

float ScaleBetweenFovs(float fromDegrees, float toDegrees) {
  auto usable = [](float degrees) { return Finite(degrees) && degrees > 0.0f && degrees < 180.0f; };
  if (!usable(fromDegrees) || !usable(toDegrees))
    return 0.0f;
  return std::tan(toDegrees * 0.5f / kDegreesPerRadian) / std::tan(fromDegrees * 0.5f / kDegreesPerRadian);
}

float ScaleToHorizontalFov(const Frustum &frustum, float fovDegrees) {
  const float current = FrustumAngleDegrees(frustum.left, frustum.right);
  return Finite(current) ? ScaleBetweenFovs(current, fovDegrees) : 0.0f;
}

void ScaleFieldOfView(Camera &camera, const Frustum &frustum, float scale) {
  if (!Finite(scale) || !(scale > 0.0f))
    return;
  // Row r (x with left/right, y with bottom/top): row = (2 * axis - (hi + lo) * row3) / (hi - lo). With every side
  // multiplied by s: row' = row / s + (hi + lo) * (1 - s) / (s * (hi - lo)) * row3.
  auto rescale = [&](int row, float lo, float hi) {
    const float span = hi - lo;
    if (!Finite(span) || span == 0.0f)
      return;
    const float shift = (hi + lo) * (1.0f - scale) / (scale * span);
    for (int column = 0; column < 4; ++column)
      camera.worldToClip[row][column] = camera.worldToClip[row][column] / scale + shift * camera.worldToClip[3][column];
  };
  rescale(0, frustum.left, frustum.right);
  rescale(1, frustum.bottom, frustum.top);
}

uint32_t Fade(uint32_t color, float alpha) {
  const float clamped = std::clamp(alpha, 0.0f, 1.0f);
  const auto a = static_cast<uint32_t>(static_cast<float>((color >> 24) & 0xFF) * clamped + 0.5f);
  return (a << 24) | (color & 0x00FFFFFF);
}

uint8_t DigitSegments(int digit) {
  static constexpr uint8_t kMasks[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  return digit >= 0 && digit <= 9 ? kMasks[digit] : 0;
}

void Batch::Triangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, uint32_t color) {
  m_vertices.push_back({a.x, a.y, 0.0f, 1.0f, color});
  m_vertices.push_back({b.x, b.y, 0.0f, 1.0f, color});
  m_vertices.push_back({c.x, c.y, 0.0f, 1.0f, color});
}

void Batch::Line(ScreenPoint a, ScreenPoint b, float width, uint32_t color) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (!(length > 0.0f) || !Finite(length))
    return;
  const float nx = -dy / length * width * 0.5f;
  const float ny = dx / length * width * 0.5f;
  const ScreenPoint p0 = {a.x + nx, a.y + ny};
  const ScreenPoint p1 = {b.x + nx, b.y + ny};
  const ScreenPoint p2 = {b.x - nx, b.y - ny};
  const ScreenPoint p3 = {a.x - nx, a.y - ny};
  Triangle(p0, p1, p2, color);
  Triangle(p0, p2, p3, color);
}

void Batch::Box(ScreenPoint centre, float half, uint32_t color) {
  const ScreenPoint p0 = {centre.x - half, centre.y - half};
  const ScreenPoint p1 = {centre.x + half, centre.y - half};
  const ScreenPoint p2 = {centre.x + half, centre.y + half};
  const ScreenPoint p3 = {centre.x - half, centre.y + half};
  Triangle(p0, p1, p2, color);
  Triangle(p0, p2, p3, color);
}

void Batch::Frame(ScreenPoint centre, float half, float width, uint32_t color) {
  const float l = centre.x - half;
  const float r = centre.x + half;
  const float t = centre.y - half;
  const float b = centre.y + half;
  const float h = width * 0.5f;
  Line({l - h, t}, {r + h, t}, width, color);
  Line({l - h, b}, {r + h, b}, width, color);
  Line({l, t}, {l, b}, width, color);
  Line({r, t}, {r, b}, width, color);
}

float Batch::Number(ScreenPoint topLeft, float height, unsigned value, uint32_t color) {
  char digits[12];
  int count = 0;
  do {
    digits[count++] = static_cast<char>(value % 10);
    value /= 10;
  } while (value && count < 12);

  const float w = height * 0.5f;
  const float stroke = std::max(1.0f, height * 0.14f);
  const float advance = w + height * 0.3f;
  const float mid = height * 0.5f;
  float x = topLeft.x;
  for (int i = count - 1; i >= 0; --i) {
    const uint8_t mask = DigitSegments(digits[i]);
    const float y = topLeft.y;
    const ScreenPoint segments[7][2] = {
        {{x, y}, {x + w, y}},                    // top
        {{x + w, y}, {x + w, y + mid}},          // top right
        {{x + w, y + mid}, {x + w, y + height}}, // bottom right
        {{x, y + height}, {x + w, y + height}},  // bottom
        {{x, y + mid}, {x, y + height}},         // bottom left
        {{x, y}, {x, y + mid}},                  // top left
        {{x, y + mid}, {x + w, y + mid}},        // middle
    };
    for (int segment = 0; segment < 7; ++segment)
      if (mask & (1u << segment))
        Line(segments[segment][0], segments[segment][1], stroke, color);
    x += advance;
  }
  return x - topLeft.x;
}

void Batch::WorldLine(const Camera &camera, const Point3 &a, const Point3 &b, float width, uint32_t color) {
  ScreenPoint sa;
  ScreenPoint sb;
  if (ProjectSegment(camera, a, b, sa, sb))
    Line(sa, sb, width, color);
}

bool Batch::WorldBox(const Camera &camera, const Point3 &p, float half, uint32_t color, ScreenPoint *where) {
  ScreenPoint s;
  if (!Project(camera, p, s))
    return false;
  if (s.x < -half || s.y < -half || s.x > camera.width + half || s.y > camera.height + half)
    return false;
  Box(s, half, color);
  if (where)
    *where = s;
  return true;
}

} // namespace Overlay
