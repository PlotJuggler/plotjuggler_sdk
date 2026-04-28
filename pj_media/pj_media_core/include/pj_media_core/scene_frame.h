#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/types.hpp"

namespace PJ {

/// Vertex topology for vector annotations. Mirrors the canonical primitive set
/// from `pj_media/docs/datatypes_2D.md` §8.
enum class AnnotationTopology : uint8_t {
  kPoints,      ///< Each point is independent.
  kLineList,    ///< Consecutive pairs form segments (0-1, 2-3, ...).
  kLineStrip,   ///< Connected polyline 0-1, 1-2, ..., n-1-n.
  kLineLoop,    ///< Like LineStrip but closes back to the first point. 4-point loop = rectangle.
};

/// 2D point in image-pixel coordinates (origin top-left).
struct Point2 {
  double x = 0.0;
  double y = 0.0;
  bool operator==(const Point2&) const = default;
};

/// 8-bit per-channel RGBA color. a=0 means transparent / disabled.
struct ColorRGBA {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
  bool operator==(const ColorRGBA&) const = default;
};

/// Vector primitive (points, lines, polygons) over an image's pixel space.
///
/// Color semantics: if `colors` is empty, the uniform `color` applies to all
/// vertices. If `colors.size() == points.size()`, per-vertex coloring is used.
/// Anything in between is implementation-defined (renderers may splat-last).
struct PointsAnnotation {
  AnnotationTopology topology = AnnotationTopology::kPoints;
  std::vector<Point2> points;
  double thickness = 2.0;
  ColorRGBA color = {0, 255, 0, 255};
  std::vector<ColorRGBA> colors;
  ColorRGBA fill_color = {0, 0, 0, 0};  ///< a=0 means no fill (LineLoop only).
  bool operator==(const PointsAnnotation&) const = default;
};

/// Filled or stroked circle in image-pixel space.
struct CircleAnnotation {
  Point2 center;
  double radius = 1.0;
  double thickness = 2.0;
  ColorRGBA color = {0, 255, 0, 255};
  ColorRGBA fill_color = {0, 0, 0, 0};
  bool operator==(const CircleAnnotation&) const = default;
};

/// Text label anchored at a pixel position.
struct TextAnnotation {
  Point2 position;
  double font_size = 14.0;
  ColorRGBA color = {255, 255, 255, 255};
  std::string text;
  bool operator==(const TextAnnotation&) const = default;
};

/// All annotations for one image at one timestamp. References its base image
/// explicitly via `image_topic` so the renderer knows which frame to overlay.
struct ImageAnnotation {
  Timestamp timestamp = 0;
  std::string image_topic;
  std::vector<PointsAnnotation> points;
  std::vector<CircleAnnotation> circles;
  std::vector<TextAnnotation> texts;
  bool operator==(const ImageAnnotation&) const = default;

  /// True if no primitives are present.
  [[nodiscard]] bool empty() const noexcept {
    return points.empty() && circles.empty() && texts.empty();
  }
};

/// Top-level output of a SceneDecoder. Wraps a list of ImageAnnotations for
/// this iteration; future iterations will extend with 3D ScenePrimitive (§7),
/// Grid (§9), etc.
struct SceneFrame {
  Timestamp timestamp = 0;
  std::vector<ImageAnnotation> annotations;
  bool operator==(const SceneFrame&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return annotations.empty();
  }
};

}  // namespace PJ
