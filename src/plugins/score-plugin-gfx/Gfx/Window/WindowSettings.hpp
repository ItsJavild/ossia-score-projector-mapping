#pragma once
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <vector>
#include <QPolygonF>
#include <QTransform>
#include <cmath>

namespace Gfx
{

struct WindowOutputSettings
{
  int width{};
  int height{};
  double rate{};
  bool viewportSize{};
  bool vsync{};
};

enum class WindowMode : int
{
  Single = 0,
  Background = 1,
  MultiWindow = 2
};

enum class SwapchainFlag : int
{
  NoFlag = 0,
  sRGB = (1 << 2)
};

// Matches QRhiSwapchainFormat
enum class SwapchainFormat : int
{
  SDR,
  HDRExtendedSrgbLinear,
  HDR10,
  HDRExtendedDisplayP3Linear
};


struct EdgeBlend
{
  float width{0.0f}; // Blend width in UV space (0.0 = no blend, 0.15 = 15% of output)
  float gamma{2.2f}; // Blend curve exponent (1.0 = linear, 2.2 = typical projector)
};

struct CornerWarp
{
  QPointF topLeft{0.0, 0.0};
  QPointF topRight{1.0, 0.0};
  QPointF bottomLeft{0.0, 1.0};
  QPointF bottomRight{1.0, 1.0};

  bool isIdentity() const noexcept
  {
    constexpr double eps = 1e-6;
    return qAbs(topLeft.x()) < eps && qAbs(topLeft.y()) < eps
           && qAbs(topRight.x() - 1.0) < eps && qAbs(topRight.y()) < eps
           && qAbs(bottomLeft.x()) < eps && qAbs(bottomLeft.y() - 1.0) < eps
           && qAbs(bottomRight.x() - 1.0) < eps && qAbs(bottomRight.y() - 1.0) < eps;
  }
};

// Optional regular lattice in output UV space. Empty keeps legacy perspective warp.
struct MeshWarp
{
  int columns{0};
  int rows{0};
  std::vector<QPointF> points;

  bool enabled() const noexcept
  {
    return columns >= 2 && rows >= 2 && columns <= 17 && rows <= 17
           && points.size() == std::size_t(columns * rows);
  }

  bool hasValidCells() const noexcept
  {
    if(!enabled()) return false;
    auto area = [](QPointF a, QPointF b, QPointF c) {
      const auto u = b - a, v = c - a;
      return u.x() * v.y() - u.y() * v.x();
    };
    for(const auto& p : points)
      if(!std::isfinite(p.x()) || !std::isfinite(p.y())) return false;
    for(int y = 0; y + 1 < rows; ++y)
      for(int x = 0; x + 1 < columns; ++x)
      {
        const auto a = points[y * columns + x], b = points[y * columns + x + 1];
        const auto c = points[(y + 1) * columns + x], d = points[(y + 1) * columns + x + 1];
        if(area(a, b, c) <= 1e-8 || area(b, d, c) <= 1e-8) return false;
      }
    return true;
  }

  void reset(int cols, int lines, const CornerWarp& corners)
  {
    columns = qBound(2, cols, 17);
    rows = qBound(2, lines, 17);
    points.clear();
    // Initialize from the existing perspective mapping, preserving calibration.
    QPolygonF src, dst;
    src << QPointF(0, 0) << QPointF(1, 0) << QPointF(1, 1) << QPointF(0, 1);
    dst << corners.topLeft << corners.topRight << corners.bottomRight << corners.bottomLeft;
    QTransform transform;
    QTransform::quadToQuad(src, dst, transform);
    for(int y = 0; y < rows; ++y)
      for(int x = 0; x < columns; ++x)
        points.push_back(transform.map(QPointF(double(x) / (columns - 1), double(y) / (rows - 1))));
  }
};

enum class OutputLockMode : int
{
  Free = 0,       // No constraints
  AspectRatio = 1, // Output window preserves input section aspect ratio
  OneToOne = 2,    // Output window size = input source rect pixels (1:1)
  FullLock = 3     // Cannot move or resize in graphics scenes
};

//! Where a new output window goes when nothing says otherwise.
static const constexpr QPoint defaultWindowPosition{100, 100};

/**
 * @brief Where to put the window of an output whose source starts at \p p.
 *
 * The desktop origin is a bad place for a window: it is hard to grab by its
 * title bar on Windows, and a tiling window manager may not map it at all --
 * on i3 the first output window stayed invisible until it was moved by one
 * pixel. Only that exact spot is moved; anywhere else is left alone.
 */
inline QPoint windowPositionForSource(QPoint p) noexcept
{
  return p.isNull() ? defaultWindowPosition : p;
}

struct OutputCalibration
{
  float red{1.f}, green{1.f}, blue{1.f};
  float gamma{1.f};
  float blackLevel{0.f};
  bool isIdentity() const noexcept
  { return red == 1.f && green == 1.f && blue == 1.f && gamma == 1.f && blackLevel == 0.f; }
};

struct OutputMapping
{
  QRectF sourceRect{0.0, 0.0, 1.0, 1.0}; // UV coords in input texture
  int screenIndex{-1};                   // -1 = default screen
  //! Not the origin: a window there is hard to grab by its title bar on
  //! Windows, and a tiling window manager may not map it at all (on i3 the
  //! first output window stayed invisible until it was moved by one pixel).
  QPoint windowPosition{defaultWindowPosition};
  QSize windowSize{1280, 720};
  bool fullscreen{false};

  // Soft-edge blending per side
  EdgeBlend blendLeft;
  EdgeBlend blendRight;
  EdgeBlend blendTop;
  EdgeBlend blendBottom;

  // 4-corner perspective warp (output UV space)
  CornerWarp cornerWarp;
  MeshWarp meshWarp;
  OutputCalibration calibration;

  OutputLockMode lockMode{OutputLockMode::Free};

  int rotation{0};     // 0, 90, 180, 270 degrees clockwise
  bool mirrorX{false}; // Flip output horizontally (after rotation)
  bool mirrorY{false}; // Flip output vertically (after rotation)
};

}
