// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/voxel_grid_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::PointField;
using sdk::VoxelGrid;

// ---------- PointField enum mapping ----------
// Numerically identical to the proto enum (UNKNOWN=0 .. FLOAT64=8); see
// point_cloud_codec.cpp. The PointField wire helpers below intentionally mirror
// that codec's local helpers — extracting a shared point_field_codec.hpp is a
// candidate follow-up once a third consumer appears (Rule of Three).

uint32_t datatypeToWire(PointField::Datatype dt) {
  return static_cast<uint32_t>(dt);
}

PointField::Datatype datatypeFromWire(uint64_t value) {
  switch (value) {
    case 1:
      return PointField::Datatype::kInt8;
    case 2:
      return PointField::Datatype::kUint8;
    case 3:
      return PointField::Datatype::kInt16;
    case 4:
      return PointField::Datatype::kUint16;
    case 5:
      return PointField::Datatype::kInt32;
    case 6:
      return PointField::Datatype::kUint32;
    case 7:
      return PointField::Datatype::kFloat32;
    case 8:
      return PointField::Datatype::kFloat64;
    case 0:
    default:
      return PointField::Datatype::kUnknown;
  }
}

void writePointField(Writer& writer, const PointField& field) {
  writer.string(1, field.name);
  writer.varint(2, field.offset);
  writer.varint(3, datatypeToWire(field.datatype));
  writer.varint(4, field.count);
}

bool decodePointField(Reader& reader, PointField& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && r.readString(out.name);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.offset = static_cast<uint32_t>(v);
        return true;
      }
      case 3: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.datatype = datatypeFromWire(v);
        return true;
      }
      case 4: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.count = static_cast<uint32_t>(v);
        return true;
      }
      default:
        return false;
    }
  });
}

bool readPointFieldIntoVector(Reader& reader, std::vector<PointField>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  PointField field;
  if (!decodePointField(nested, field)) {
    return false;
  }
  out.push_back(std::move(field));
  return true;
}

bool readBytesIntoGrid(Reader& reader, VoxelGrid& out) {
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

}  // namespace

std::vector<uint8_t> serializeVoxelGrid(const VoxelGrid& grid) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, grid.timestamp_ns); });
  writer.string(2, grid.frame_id);
  writer.message(3, [&](Writer& nested) { builtin_wire::writePose(nested, grid.origin); });
  writer.message(4, [&](Writer& nested) { builtin_wire::writeVector3(nested, grid.cell_size); });
  writer.varint(5, grid.column_count);
  writer.varint(6, grid.row_count);
  writer.varint(7, grid.slice_count);
  writer.varint(8, grid.cell_stride);
  writer.varint(9, grid.row_stride);
  writer.varint(10, grid.slice_stride);
  for (const auto& field : grid.fields) {
    writer.message(11, [&](Writer& nested) { writePointField(nested, field); });
  }
  writer.bytes(12, grid.data.data(), grid.data.size());

  return out;
}

Expected<sdk::VoxelGrid> deserializeVoxelGrid(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("VoxelGrid wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::VoxelGrid grid;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, grid.timestamp_ns);
      case 2:
        return tag.type == WireType::kLengthDelimited && r.readString(grid.frame_id);
      case 3:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, grid.origin);
      case 4:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readVector3Message(r, grid.cell_size);
      case 5:
        return readVarintInto(r, tag.type, grid.column_count);
      case 6:
        return readVarintInto(r, tag.type, grid.row_count);
      case 7:
        return readVarintInto(r, tag.type, grid.slice_count);
      case 8:
        return readVarintInto(r, tag.type, grid.cell_stride);
      case 9:
        return readVarintInto(r, tag.type, grid.row_stride);
      case 10:
        return readVarintInto(r, tag.type, grid.slice_stride);
      case 11:
        return tag.type == WireType::kLengthDelimited && readPointFieldIntoVector(r, grid.fields);
      case 12:
        return tag.type == WireType::kLengthDelimited && readBytesIntoGrid(r, grid);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("VoxelGrid wire: decode failed"));
  }

  return grid;
}

}  // namespace PJ
