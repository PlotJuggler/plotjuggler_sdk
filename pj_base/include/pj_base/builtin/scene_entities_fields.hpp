/**
 * @file scene_entities_fields.hpp
 * @brief `FieldTable` specializations for `scene_entities.hpp`'s structs.
 *
 * `Pose`/`Vector3` are tabled by `frame_transforms_fields.hpp` and
 * `ColorRGBA` by `image_annotations_fields.hpp` — `scene_entities.hpp`
 * reuses those exact types (see its own file comment), so this header
 * includes both rather than re-specializing `FieldTable` for them, which
 * would be an ODR violation (two definitions of the same specialization).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/frame_transforms_fields.hpp"   // Pose, Vector3
#include "pj_base/builtin/image_annotations_fields.hpp"  // ColorRGBA
#include "pj_base/builtin/scene_entities.hpp"

namespace PJ::sdk {

/// Fields of `Point3`: `x`, `y`, `z`.
template <>
struct FieldTable<Point3> {
  static constexpr std::array<FieldDescriptor, 3> fields{
      field<&Point3::x>("x"),
      field<&Point3::y>("y"),
      field<&Point3::z>("z"),
  };
  static constexpr FieldTableView view{"Point3", Span<const FieldDescriptor>(fields)};
};

/// Fields of `KeyValuePair`: `key`, `value`.
template <>
struct FieldTable<KeyValuePair> {
  static constexpr std::array<FieldDescriptor, 2> fields{
      field<&KeyValuePair::key>("key"),
      field<&KeyValuePair::value>("value"),
  };
  static constexpr FieldTableView view{"KeyValuePair", Span<const FieldDescriptor>(fields)};
};

/// Fields of `ArrowPrimitive`: `pose`, `shaft_length`, `shaft_diameter`,
/// `head_length`, `head_diameter`, `color`.
template <>
struct FieldTable<ArrowPrimitive> {
  static constexpr std::array<FieldDescriptor, 6> fields{
      field<&ArrowPrimitive::pose>("pose"),
      field<&ArrowPrimitive::shaft_length>("shaft_length"),
      field<&ArrowPrimitive::shaft_diameter>("shaft_diameter"),
      field<&ArrowPrimitive::head_length>("head_length"),
      field<&ArrowPrimitive::head_diameter>("head_diameter"),
      field<&ArrowPrimitive::color>("color"),
  };
  static constexpr FieldTableView view{"ArrowPrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `CubePrimitive`: `pose`, `size`, `color`.
template <>
struct FieldTable<CubePrimitive> {
  static constexpr std::array<FieldDescriptor, 3> fields{
      field<&CubePrimitive::pose>("pose"),
      field<&CubePrimitive::size>("size"),
      field<&CubePrimitive::color>("color"),
  };
  static constexpr FieldTableView view{"CubePrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `SpherePrimitive`: `pose`, `size`, `color`.
template <>
struct FieldTable<SpherePrimitive> {
  static constexpr std::array<FieldDescriptor, 3> fields{
      field<&SpherePrimitive::pose>("pose"),
      field<&SpherePrimitive::size>("size"),
      field<&SpherePrimitive::color>("color"),
  };
  static constexpr FieldTableView view{"SpherePrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `CylinderPrimitive`: `pose`, `size`, `bottom_scale`,
/// `top_scale`, `color`.
template <>
struct FieldTable<CylinderPrimitive> {
  static constexpr std::array<FieldDescriptor, 5> fields{
      field<&CylinderPrimitive::pose>("pose"),
      field<&CylinderPrimitive::size>("size"),
      field<&CylinderPrimitive::bottom_scale>("bottom_scale"),
      field<&CylinderPrimitive::top_scale>("top_scale"),
      field<&CylinderPrimitive::color>("color"),
  };
  static constexpr FieldTableView view{"CylinderPrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `LinePrimitive`: `type` (enum), `pose`, `thickness`,
/// `scale_invariant`, `points` (list of `Point3`), `color`, `colors` (list
/// of `ColorRGBA`), `indices` (scalar list of `uint32_t` -> kNumber).
template <>
struct FieldTable<LinePrimitive> {
  static constexpr std::array<FieldDescriptor, 8> fields{
      field<&LinePrimitive::type>("type"),           field<&LinePrimitive::pose>("pose"),
      field<&LinePrimitive::thickness>("thickness"), field<&LinePrimitive::scale_invariant>("scale_invariant"),
      field<&LinePrimitive::points>("points"),       field<&LinePrimitive::color>("color"),
      field<&LinePrimitive::colors>("colors"),       field<&LinePrimitive::indices>("indices"),
  };
  static constexpr FieldTableView view{"LinePrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `TrianglePrimitive`: `pose`, `points` (list of `Point3`),
/// `color`, `colors` (list of `ColorRGBA`), `indices` (scalar list of
/// `uint32_t` -> kNumber).
template <>
struct FieldTable<TrianglePrimitive> {
  static constexpr std::array<FieldDescriptor, 5> fields{
      field<&TrianglePrimitive::pose>("pose"),       field<&TrianglePrimitive::points>("points"),
      field<&TrianglePrimitive::color>("color"),     field<&TrianglePrimitive::colors>("colors"),
      field<&TrianglePrimitive::indices>("indices"),
  };
  static constexpr FieldTableView view{"TrianglePrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `TextPrimitive`: `pose`, `billboard`, `font_size`,
/// `scale_invariant`, `color`, `text`.
template <>
struct FieldTable<TextPrimitive> {
  static constexpr std::array<FieldDescriptor, 6> fields{
      field<&TextPrimitive::pose>("pose"),           field<&TextPrimitive::billboard>("billboard"),
      field<&TextPrimitive::font_size>("font_size"), field<&TextPrimitive::scale_invariant>("scale_invariant"),
      field<&TextPrimitive::color>("color"),         field<&TextPrimitive::text>("text"),
  };
  static constexpr FieldTableView view{"TextPrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `AxesPrimitive`: `pose`, `length`, `thickness`, `scale_invariant`.
template <>
struct FieldTable<AxesPrimitive> {
  static constexpr std::array<FieldDescriptor, 4> fields{
      field<&AxesPrimitive::pose>("pose"),
      field<&AxesPrimitive::length>("length"),
      field<&AxesPrimitive::thickness>("thickness"),
      field<&AxesPrimitive::scale_invariant>("scale_invariant"),
  };
  static constexpr FieldTableView view{"AxesPrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `ModelPrimitive`: `pose`, `scale`, `color`, `override_color`,
/// `url`, `media_type`, `data` (scalar list of `uint8_t` -> kNumber; a plain
/// `std::vector<uint8_t>` member here, unlike `PointCloud::data`, which is a
/// `Span`+`BufferAnchor` pair described through a `kBuffer` descriptor such as `pointCloudDataField()` instead).
template <>
struct FieldTable<ModelPrimitive> {
  static constexpr std::array<FieldDescriptor, 7> fields{
      field<&ModelPrimitive::pose>("pose"),   field<&ModelPrimitive::scale>("scale"),
      field<&ModelPrimitive::color>("color"), field<&ModelPrimitive::override_color>("override_color"),
      field<&ModelPrimitive::url>("url"),     field<&ModelPrimitive::media_type>("media_type"),
      field<&ModelPrimitive::data>("data"),
  };
  static constexpr FieldTableView view{"ModelPrimitive", Span<const FieldDescriptor>(fields)};
};

/// Fields of `SceneEntity`: `timestamp` (int64), `frame_id`, `id`,
/// `lifetime_ns` (int64), `frame_locked`, `metadata` (list of
/// `KeyValuePair`), and the nine primitive lists in `SceneEntity`'s
/// declaration order (`arrows`, `cubes`, `spheres`, `cylinders`, `lines`,
/// `triangles`, `texts`, `models`, `axes`).
template <>
struct FieldTable<SceneEntity> {
  static constexpr std::array<FieldDescriptor, 15> fields{
      field<&SceneEntity::timestamp>("timestamp"),
      field<&SceneEntity::frame_id>("frame_id"),
      field<&SceneEntity::id>("id"),
      field<&SceneEntity::lifetime_ns>("lifetime_ns"),
      field<&SceneEntity::frame_locked>("frame_locked"),
      field<&SceneEntity::metadata>("metadata"),
      field<&SceneEntity::arrows>("arrows"),
      field<&SceneEntity::cubes>("cubes"),
      field<&SceneEntity::spheres>("spheres"),
      field<&SceneEntity::cylinders>("cylinders"),
      field<&SceneEntity::lines>("lines"),
      field<&SceneEntity::triangles>("triangles"),
      field<&SceneEntity::texts>("texts"),
      field<&SceneEntity::models>("models"),
      field<&SceneEntity::axes>("axes"),
  };
  static constexpr FieldTableView view{"SceneEntity", Span<const FieldDescriptor>(fields)};
};

/// Fields of `SceneEntityDeletion`: `type` (enum), `timestamp` (int64), `id`.
template <>
struct FieldTable<SceneEntityDeletion> {
  static constexpr std::array<FieldDescriptor, 3> fields{
      field<&SceneEntityDeletion::type>("type"),
      field<&SceneEntityDeletion::timestamp>("timestamp"),
      field<&SceneEntityDeletion::id>("id"),
  };
  static constexpr FieldTableView view{"SceneEntityDeletion", Span<const FieldDescriptor>(fields)};
};

/// Fields of `SceneEntities`: `entities` (list of `SceneEntity`),
/// `deletions` (list of `SceneEntityDeletion`).
template <>
struct FieldTable<SceneEntities> {
  static constexpr std::array<FieldDescriptor, 2> fields{
      field<&SceneEntities::entities>("entities"),
      field<&SceneEntities::deletions>("deletions"),
  };
  static constexpr FieldTableView view{"SceneEntities", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
