/**
 * @file image_annotations_fields.hpp
 * @brief `FieldTable` specializations for `image_annotations.hpp`'s structs,
 *        so a generic script binder can expose their fields by name.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/image_annotations.hpp"

namespace PJ::sdk {

/// Fields of `Point2`: `x`, `y`.
template <>
struct FieldTable<Point2> {
  static constexpr std::array<FieldDescriptor, 2> fields{
      field<&Point2::x>("x"),
      field<&Point2::y>("y"),
  };
  static constexpr FieldTableView view{"Point2", Span<const FieldDescriptor>(fields)};
};

/// Fields of `ColorRGBA`: `r`, `g`, `b`, `a` (each `uint8_t`, read/written as `double`).
template <>
struct FieldTable<ColorRGBA> {
  static constexpr std::array<FieldDescriptor, 4> fields{
      field<&ColorRGBA::r>("r"),
      field<&ColorRGBA::g>("g"),
      field<&ColorRGBA::b>("b"),
      field<&ColorRGBA::a>("a"),
  };
  static constexpr FieldTableView view{"ColorRGBA", Span<const FieldDescriptor>(fields)};
};

/// Fields of `PointsAnnotation`: `topology` (enum), `points` (list of
/// `Point2`), `thickness`, `color`, `colors` (list of `ColorRGBA`), `fill_color`.
template <>
struct FieldTable<PointsAnnotation> {
  static constexpr std::array<FieldDescriptor, 6> fields{
      field<&PointsAnnotation::topology>("topology"),   field<&PointsAnnotation::points>("points"),
      field<&PointsAnnotation::thickness>("thickness"), field<&PointsAnnotation::color>("color"),
      field<&PointsAnnotation::colors>("colors"),       field<&PointsAnnotation::fill_color>("fill_color"),
  };
  static constexpr FieldTableView view{"PointsAnnotation", Span<const FieldDescriptor>(fields)};
};

/// Fields of `CircleAnnotation`: `center` (Point2), `radius`, `thickness`,
/// `color`, `fill_color`.
template <>
struct FieldTable<CircleAnnotation> {
  static constexpr std::array<FieldDescriptor, 5> fields{
      field<&CircleAnnotation::center>("center"),         field<&CircleAnnotation::radius>("radius"),
      field<&CircleAnnotation::thickness>("thickness"),   field<&CircleAnnotation::color>("color"),
      field<&CircleAnnotation::fill_color>("fill_color"),
  };
  static constexpr FieldTableView view{"CircleAnnotation", Span<const FieldDescriptor>(fields)};
};

/// Fields of `TextAnnotation`: `position` (Point2), `font_size`, `color`, `text`.
template <>
struct FieldTable<TextAnnotation> {
  static constexpr std::array<FieldDescriptor, 4> fields{
      field<&TextAnnotation::position>("position"),
      field<&TextAnnotation::font_size>("font_size"),
      field<&TextAnnotation::color>("color"),
      field<&TextAnnotation::text>("text"),
  };
  static constexpr FieldTableView view{"TextAnnotation", Span<const FieldDescriptor>(fields)};
};

/// Fields of `ImageAnnotations`: `timestamp` (int64), `image_topic`
/// (string), `points`, `circles`, `texts` (lists of the primitive structs above).
template <>
struct FieldTable<ImageAnnotations> {
  static constexpr std::array<FieldDescriptor, 5> fields{
      field<&ImageAnnotations::timestamp>("timestamp"), field<&ImageAnnotations::image_topic>("image_topic"),
      field<&ImageAnnotations::points>("points"),       field<&ImageAnnotations::circles>("circles"),
      field<&ImageAnnotations::texts>("texts"),
  };
  static constexpr FieldTableView view{"ImageAnnotations", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
