/**
 * @file frame_transforms_fields.hpp
 * @brief `FieldTable` specializations for `frame_transforms.hpp`'s structs,
 *        so a generic script binder can expose their fields by name.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/frame_transforms.hpp"

namespace PJ::sdk {

/// Fields of `Vector2`: `x`, `y`.
template <>
struct FieldTable<Vector2> {
  static constexpr std::array fields{
      field<&Vector2::x>("x"),
      field<&Vector2::y>("y"),
  };
  static constexpr FieldTableView view{"Vector2", Span<const FieldDescriptor>(fields)};
};

/// Fields of `Vector3`: `x`, `y`, `z`.
template <>
struct FieldTable<Vector3> {
  static constexpr std::array fields{
      field<&Vector3::x>("x"),
      field<&Vector3::y>("y"),
      field<&Vector3::z>("z"),
  };
  static constexpr FieldTableView view{"Vector3", Span<const FieldDescriptor>(fields)};
};

/// Fields of `Quaternion`: `x`, `y`, `z`, `w`.
template <>
struct FieldTable<Quaternion> {
  static constexpr std::array fields{
      field<&Quaternion::x>("x"),
      field<&Quaternion::y>("y"),
      field<&Quaternion::z>("z"),
      field<&Quaternion::w>("w"),
  };
  static constexpr FieldTableView view{"Quaternion", Span<const FieldDescriptor>(fields)};
};

/// Fields of `Pose`: `position` (Vector3), `orientation` (Quaternion).
template <>
struct FieldTable<Pose> {
  static constexpr std::array fields{
      field<&Pose::position>("position"),
      field<&Pose::orientation>("orientation"),
  };
  static constexpr FieldTableView view{"Pose", Span<const FieldDescriptor>(fields)};
};

/// Fields of `FrameTransform`: `timestamp` (int64), `parent_frame_id`,
/// `child_frame_id` (strings), `translation` (Vector3), `rotation` (Quaternion).
template <>
struct FieldTable<FrameTransform> {
  static constexpr std::array fields{
      field<&FrameTransform::timestamp>("timestamp"),
      field<&FrameTransform::parent_frame_id>("parent_frame_id"),
      field<&FrameTransform::child_frame_id>("child_frame_id"),
      field<&FrameTransform::translation>("translation"),
      field<&FrameTransform::rotation>("rotation"),
  };
  static constexpr FieldTableView view{"FrameTransform", Span<const FieldDescriptor>(fields)};
};

/// Fields of `FrameTransforms`: `transforms` (list of `FrameTransform`).
template <>
struct FieldTable<FrameTransforms> {
  static constexpr std::array fields{
      field<&FrameTransforms::transforms>("transforms"),
  };
  static constexpr FieldTableView view{"FrameTransforms", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
