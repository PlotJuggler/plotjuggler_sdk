/**
 * @file scene_entities.hpp
 * @brief Procedural 3D scene primitives + SceneEntity container + batch.
 *
 * SceneEntities is the workhorse for marker-style 3D visualization (the
 * visualization_msgs/MarkerArray equivalent). A SceneEntity bundles
 * heterogeneous primitives sharing a frame_id and timestamp; SceneEntities
 * is the batch container shipped on a topic.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/builtin/frame_transforms.hpp"   // for Pose, Vector3
#include "pj_base/builtin/image_annotations.hpp"  // for ColorRGBA
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Position in 3D space.
struct Point3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  bool operator==(const Point3&) const = default;
};

/// Topology of a LinePrimitive's vertex list.
enum class LineType : uint8_t {
  kLineStrip = 0,  ///< 0-1, 1-2, ..., (n-1)-n
  kLineLoop = 1,   ///< like LineStrip, plus closing edge n-0
  kLineList = 2,   ///< 0-1, 2-3, 4-5, ...
};

/// Arrow primitive. Tail at pose.position; identity orientation points along +x.
struct ArrowPrimitive {
  Pose pose;
  double shaft_length = 0.0;
  double shaft_diameter = 0.0;
  double head_length = 0.0;
  double head_diameter = 0.0;
  ColorRGBA color;
  bool operator==(const ArrowPrimitive&) const = default;
};

/// Cuboid (or rectangular prism) centered at pose.position with extents `size`.
struct CubePrimitive {
  Pose pose;
  Vector3 size;  ///< Side lengths along local x, y, z.
  ColorRGBA color;
  bool operator==(const CubePrimitive&) const = default;
};

/// Sphere or ellipsoid centered at pose.position. Non-uniform `size` yields an ellipsoid.
struct SpherePrimitive {
  Pose pose;
  Vector3 size;  ///< Diameter along local x, y, z.
  ColorRGBA color;
  bool operator==(const SpherePrimitive&) const = default;
};

/// Cylinder / cone / truncated cone. Flat faces perpendicular to local z.
/// `bottom_scale` / `top_scale` (in 0..1) collapse the respective face toward an apex.
struct CylinderPrimitive {
  Pose pose;
  Vector3 size;  ///< Bounding box (diameter x, diameter y, height z).
  double bottom_scale = 1.0;
  double top_scale = 1.0;
  ColorRGBA color;
  bool operator==(const CylinderPrimitive&) const = default;
};

/// Polyline or line-list primitive. `pose` is the origin of the local
/// frame in which `points` are interpreted.
struct LinePrimitive {
  LineType type = LineType::kLineStrip;
  Pose pose;
  double thickness = 0.0;
  bool scale_invariant = false;  ///< true => thickness in screen pixels.
  std::vector<Point3> points;
  ColorRGBA color;                ///< Solid color (used when `colors` is empty).
  std::vector<ColorRGBA> colors;  ///< Per-vertex colors; size must equal points.size() when non-empty.
  std::vector<uint32_t> indices;  ///< Optional vertex index buffer (GL-style). Empty => 0..N-1.
  bool operator==(const LinePrimitive&) const = default;
};

/// Triangle-list primitive. Vertices consumed in triples: (0,1,2), (3,4,5), ...
struct TrianglePrimitive {
  Pose pose;
  std::vector<Point3> points;
  ColorRGBA color;
  std::vector<ColorRGBA> colors;
  std::vector<uint32_t> indices;
  bool operator==(const TrianglePrimitive&) const = default;
};

/// Text label anchored at pose.position. Identity orientation flows along +x in the xy-plane.
struct TextPrimitive {
  Pose pose;
  bool billboard = false;  ///< When true, always faces the camera and ignores pose.orientation.
  double font_size = 0.0;
  bool scale_invariant = false;  ///< true => font_size in screen pixels.
  ColorRGBA color;
  std::string text;
  bool operator==(const TextPrimitive&) const = default;
};

/// Coordinate-axes glyph at pose. Renders three arrows: X (red), Y (green), Z (blue).
/// Used to visualize TF frames, target poses, or any oriented reference.
struct AxesPrimitive {
  Pose pose;
  double length = 0.0;
  double thickness = 0.0;
  bool scale_invariant = false;  ///< true => length in screen pixels.
  bool operator==(const AxesPrimitive&) const = default;
};

/// A visual element in a 3D scene composed of multiple primitives, all
/// sharing the same frame of reference and timestamp.
///
/// Identity: (topic, id) is the deduplication key. Republishing with the
/// same id on the same topic replaces the previous entity.
///
/// Lifetime: zero means persist until replaced or deleted; otherwise the
/// entity is removed `lifetime_ns` after `timestamp`.
///
/// frame_locked: when true, the entity tracks `frame_id` as it moves; when
/// false, it is stamped into the fixed frame at publish time.
struct SceneEntity {
  Timestamp timestamp = 0;
  std::string frame_id;
  std::string id;
  int64_t lifetime_ns = 0;  ///< 0 means persist until replaced.
  bool frame_locked = false;

  std::vector<ArrowPrimitive> arrows;
  std::vector<CubePrimitive> cubes;
  std::vector<SpherePrimitive> spheres;
  std::vector<CylinderPrimitive> cylinders;
  std::vector<LinePrimitive> lines;
  std::vector<TrianglePrimitive> triangles;
  std::vector<TextPrimitive> texts;
  std::vector<AxesPrimitive> axes;

  bool operator==(const SceneEntity&) const = default;
};

/// Batch of scene entities published together.
struct SceneEntities {
  std::vector<SceneEntity> entities;
  bool operator==(const SceneEntities&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return entities.empty();
  }
};

}  // namespace sdk
}  // namespace PJ
