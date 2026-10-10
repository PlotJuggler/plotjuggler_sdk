/**
 * @file camera_info_fields.hpp
 * @brief `FieldTable` specialization for `camera_info.hpp`'s `CameraInfo`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/camera_info.hpp"
#include "pj_base/builtin/field_table.hpp"

namespace PJ::sdk {

/// Fields of `CameraInfo`. `K` and `R` are fixed-size lists of 9 numbers,
/// `P` a fixed-size list of 12 (`std::array<double, N>` — see
/// `field_table.hpp`'s `kList` fixed-size case, written through
/// `list_replace` rather than `list_emplace`/`list_clear`); `D` is a
/// growable list of numbers (`std::vector<double>`, size depends on
/// `distortion_model`).
template <>
struct FieldTable<CameraInfo> {
  static constexpr std::array fields{
      field<&CameraInfo::timestamp_ns>("timestamp_ns"),
      field<&CameraInfo::frame_id>("frame_id"),
      field<&CameraInfo::width>("width"),
      field<&CameraInfo::height>("height"),
      field<&CameraInfo::distortion_model>("distortion_model"),
      field<&CameraInfo::D>("D"),
      field<&CameraInfo::K>("K"),
      field<&CameraInfo::R>("R"),
      field<&CameraInfo::P>("P"),
  };
  static constexpr FieldTableView view{"CameraInfo", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
