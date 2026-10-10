/**
 * @file point_cloud_fields.hpp
 * @brief `FieldTable` specializations for `point_cloud.hpp`'s structs.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/point_cloud.hpp"

namespace PJ::sdk {

/// Fields of `PointField`: `name`, `offset`, `datatype` (enum), `count`.
template <>
struct FieldTable<PointField> {
  static constexpr std::array fields{
      field<&PointField::name>("name"),
      field<&PointField::offset>("offset"),
      field<&PointField::datatype>("datatype"),
      field<&PointField::count>("count"),
  };
  static constexpr FieldTableView view{"PointField", Span<const FieldDescriptor>(fields)};
};

/// `kBuffer` descriptor for `PointCloud::data`. `buffer()` resolves the packed
/// record bytes plus a per-channel layout read from `fields` / `point_step` /
/// `row_step` / `width` / `height` / `is_bigendian` (record count is
/// `width * height`, with `height == 0` read as one row; a zero `point_step`
/// yields zero records). `buffer_assign()` is `detail::assignPayloadBytes`.
/// `anchor` itself is not a table field.
[[nodiscard]] constexpr FieldDescriptor pointCloudDataField(std::string_view name) {
  FieldDescriptor d{};
  d.name = name;
  d.kind = FieldKind::kBuffer;
  d.buffer = [](const void* p) -> BufferLayout {
    const auto& cloud = *static_cast<const PointCloud*>(p);
    BufferLayout layout;
    layout.bytes = cloud.data;
    layout.record_step = cloud.point_step;
    const uint64_t rows = cloud.height == 0 ? 1 : cloud.height;
    layout.record_count = cloud.point_step == 0 ? 0 : static_cast<uint64_t>(cloud.width) * rows;
    layout.row_step = cloud.row_step;
    layout.is_bigendian = cloud.is_bigendian;
    layout.channels.reserve(cloud.fields.size());
    for (const PointField& f : cloud.fields) {
      layout.channels.push_back(
          BufferLayout::Channel{
              .name = f.name, .offset = f.offset, .datatype = static_cast<uint8_t>(f.datatype), .count = f.count});
    }
    return layout;
  };
  d.buffer_assign = &detail::assignPayloadBytes<PointCloud>;
  return d;
}

/// Fields of `PointCloud`. Every member is an ordinary field except `data`
/// (`pointCloudDataField`); `anchor` is written through that descriptor's
/// `buffer_assign()` and is not listed.
template <>
struct FieldTable<PointCloud> {
  static constexpr std::array fields{
      field<&PointCloud::width>("width"),
      field<&PointCloud::height>("height"),
      field<&PointCloud::point_step>("point_step"),
      field<&PointCloud::row_step>("row_step"),
      field<&PointCloud::is_bigendian>("is_bigendian"),
      field<&PointCloud::is_dense>("is_dense"),
      field<&PointCloud::frame_id>("frame_id"),
      field<&PointCloud::fields>("fields"),
      pointCloudDataField("data"),
      field<&PointCloud::timestamp_ns>("timestamp_ns"),
  };
  static constexpr FieldTableView view{"PointCloud", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
