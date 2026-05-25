// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/occupancy_grid_codec.hpp"

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
using sdk::OccupancyGrid;

bool readBytesIntoGrid(Reader& reader, OccupancyGrid& out) {
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

}  // namespace

std::vector<uint8_t> serializeOccupancyGrid(const OccupancyGrid& grid) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, grid.timestamp_ns); });
  writer.string(2, grid.frame_id);
  writer.message(3, [&](Writer& nested) { builtin_wire::writePose(nested, grid.origin); });
  writer.doubleField(4, grid.resolution);
  writer.varint(5, grid.width);
  writer.varint(6, grid.height);
  writer.bytes(7, grid.data.data(), grid.data.size());

  return out;
}

Expected<sdk::OccupancyGrid> deserializeOccupancyGrid(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("OccupancyGrid wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::OccupancyGrid grid;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, grid.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(grid.frame_id);
      case 3:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readPoseMessage(r, grid.origin);
      case 4:
        if (tag.type != WireType::kFixed64) {
          return false;
        }
        return r.readDouble(grid.resolution);
      case 5: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        grid.width = static_cast<uint32_t>(v);
        return true;
      }
      case 6: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        grid.height = static_cast<uint32_t>(v);
        return true;
      }
      case 7:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return readBytesIntoGrid(r, grid);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("OccupancyGrid wire: decode failed"));
  }

  return grid;
}

}  // namespace PJ
