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

/// A 3D model (mesh asset) primitive. Mirrors Foxglove's ModelPrimitive: the
/// source is either `url` (a resolvable resource, e.g. package://… or file://…)
/// or inline `data` tagged by `media_type`. `color` tints the model only when
/// `override_color` is true; otherwise the model's embedded materials are used.
struct ModelPrimitive {
  Pose pose;
  Vector3 scale;  ///< Per-axis scale factor applied to the model.
  ColorRGBA color;
  bool override_color = false;  ///< When true, `color` replaces the model's own materials.
  std::string url;              ///< Resource URI of the model (empty when inline).
  std::string media_type;       ///< MIME type of inline `data` (e.g. "model/gltf-binary").
  std::vector<uint8_t> data;    ///< Inline model bytes (empty when `url` is used).
  bool operator==(const ModelPrimitive&) const = default;
};

/// Arbitrary key/value metadata attached to a SceneEntity (mirrors Foxglove's
/// KeyValuePair). Keys should be unique within an entity.
struct KeyValuePair {
  std::string key;
  std::string value;
  bool operator==(const KeyValuePair&) const = default;
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

  std::vector<KeyValuePair> metadata;

  // Primitive lists, in Foxglove SceneEntity field order. `axes` is a
  // PlotJuggler extension with no Foxglove counterpart and sorts last.
  std::vector<ArrowPrimitive> arrows;
  std::vector<CubePrimitive> cubes;
  std::vector<SpherePrimitive> spheres;
  std::vector<CylinderPrimitive> cylinders;
  std::vector<LinePrimitive> lines;
  std::vector<TrianglePrimitive> triangles;
  std::vector<TextPrimitive> texts;
  std::vector<ModelPrimitive> models;
  std::vector<AxesPrimitive> axes;

  bool operator==(const SceneEntity&) const = default;
};

/// A command to remove previously-published entities on a topic. Mirrors
/// Foxglove's SceneEntityDeletion. Lets a snapshot-based producer express the
/// removal half of a stateful stream (e.g. ROS Marker DELETE / DELETEALL)
/// without an out-of-band channel. A consumer applies deletions against the
/// entities it has accumulated for the topic.
struct SceneEntityDeletion {
  enum class Type : uint8_t {
    kMatchingId = 0,  ///< Delete the entity on this topic whose `id` matches.
    kAll = 1,         ///< Delete every entity on this topic.
  };
  Type type = Type::kMatchingId;
  Timestamp timestamp = 0;  ///< Bounds the deletion to entities at or before this time.
  std::string id;           ///< Entity id to match when `type == kMatchingId`.
  bool operator==(const SceneEntityDeletion&) const = default;
};

/// Batch of scene entities published together. A self-contained snapshot:
/// `entities` are added/replaced (by `(topic, id)`), `deletions` remove prior
/// entities. Entities not present and not deleted persist (subject to lifetime).
struct SceneEntities {
  std::vector<SceneEntity> entities;
  std::vector<SceneEntityDeletion> deletions;
  bool operator==(const SceneEntities&) const = default;

  [[nodiscard]] bool empty() const noexcept {
    return entities.empty() && deletions.empty();
  }
};

}  // namespace sdk
}  // namespace PJ
