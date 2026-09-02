// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/grid_map_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "point_field_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::GridMap;
using sdk::PointField;

bool readVarintInto(Reader& reader, WireType type, uint32_t& out) {
  if (type != WireType::kVarint) {
    return false;
  }
  uint64_t v = 0;
  if (!reader.readVarint(v)) {
    return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

bool readBytesIntoGrid(Reader& reader, GridMap& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  auto owned = std::make_shared<std::vector<uint8_t>>(data, data + size);
  out.data = Span<const uint8_t>(owned->data(), owned->size());
  out.anchor = owned;
  return true;
}

// `check_data` is off for a header-only wire (the functional-v2 splice form,
// whose bytes the host attaches afterwards); everything else is checked
// regardless of whether the grid has cells.
Expected<void> validateLayout(const GridMap& grid, bool check_data) {
  for (const auto& field : grid.fields) {
    if (field.datatype == PointField::Datatype::kUnknown) {
      return unexpected(std::string("GridMap: field '") + field.name + "' has an unknown datatype");
    }
    if (field.count == 0) {
      return unexpected(std::string("GridMap: field '") + field.name + "' has a zero count");
    }
    const uint64_t end = static_cast<uint64_t>(field.offset) +
                         (static_cast<uint64_t>(sdk::bytesPerElement(field.datatype)) * field.count);
    if (end > grid.cell_stride) {
      return unexpected(std::string("GridMap: field '") + field.name + "' reaches past cell_stride");
    }
  }
  const bool has_cells = grid.column_count > 0 && grid.row_count > 0;
  if (!has_cells) {
    return {};
  }
  if (grid.cell_stride == 0 || grid.row_stride == 0) {
    return unexpected(std::string("GridMap: zero stride with cells declared"));
  }
  if (static_cast<uint64_t>(grid.row_stride) < static_cast<uint64_t>(grid.column_count) * grid.cell_stride) {
    return unexpected(std::string("GridMap: row_stride shorter than column_count * cell_stride"));
  }
  if (check_data && static_cast<uint64_t>(grid.data.size()) < static_cast<uint64_t>(grid.row_count) * grid.row_stride) {
    return unexpected(std::string("GridMap: data shorter than row_count * row_stride"));
  }
  return {};
}

}  // namespace

Expected<void> validateGridMap(const sdk::GridMap& grid) {
  return validateLayout(grid, /*check_data=*/true);
}

std::vector<uint8_t> serializeGridMap(const GridMap& grid) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, grid.timestamp_ns); });
  writer.string(2, grid.frame_id);
  writer.message(3, [&](Writer& nested) { builtin_wire::writePose(nested, grid.origin); });
  writer.message(4, [&](Writer& nested) { builtin_wire::writeVector2(nested, grid.cell_size); });
  writer.varint(5, grid.column_count);
  writer.varint(6, grid.row_count);
  writer.varint(7, grid.cell_stride);
  writer.varint(8, grid.row_stride);
  for (const auto& field : grid.fields) {
    writer.message(9, [&](Writer& nested) { builtin_wire::writePointField(nested, field); });
  }
  writer.bytes(10, grid.data.data(), grid.data.size());

  return out;
}

Expected<sdk::GridMap> deserializeGridMap(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("GridMap wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::GridMap grid;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, grid.timestamp_ns);
      case 2:
        return tag.type == WireType::kLengthDelimited && r.readString(grid.frame_id);
      case 3:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, grid.origin);
      case 4:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readVector2Message(r, grid.cell_size);
      case 5:
        return readVarintInto(r, tag.type, grid.column_count);
      case 6:
        return readVarintInto(r, tag.type, grid.row_count);
      case 7:
        return readVarintInto(r, tag.type, grid.cell_stride);
      case 8:
        return readVarintInto(r, tag.type, grid.row_stride);
      case 9:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPointFieldIntoVector(r, grid.fields);
      case 10:
        return tag.type == WireType::kLengthDelimited && readBytesIntoGrid(r, grid);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("GridMap wire: decode failed"));
  }
  // A wire without `data` is the spliced form: the bytes are attached later,
  // so their length is validated then (validateGridMap), not here.
  if (auto valid = validateLayout(grid, /*check_data=*/!grid.data.empty()); !valid) {
    return unexpected(std::move(valid).error());
  }

  return grid;
}

}  // namespace PJ
