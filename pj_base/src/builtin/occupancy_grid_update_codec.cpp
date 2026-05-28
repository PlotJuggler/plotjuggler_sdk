// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/occupancy_grid_update_codec.hpp"

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
using sdk::OccupancyGridUpdate;

bool readBytesIntoUpdate(Reader& reader, OccupancyGridUpdate& out) {
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

std::vector<uint8_t> serializeOccupancyGridUpdate(const OccupancyGridUpdate& update) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, update.timestamp_ns); });
  writer.string(2, update.frame_id);
  // x / y are signed cell offsets. Round-trip the full int32 range by writing
  // the 32-bit pattern as a varint (zero-extended) and reinterpreting on read.
  writer.varint(3, static_cast<uint32_t>(update.x));
  writer.varint(4, static_cast<uint32_t>(update.y));
  writer.varint(5, update.width);
  writer.varint(6, update.height);
  writer.bytes(7, update.data.data(), update.data.size());

  return out;
}

Expected<sdk::OccupancyGridUpdate> deserializeOccupancyGridUpdate(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("OccupancyGridUpdate wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::OccupancyGridUpdate update;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, update.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(update.frame_id);
      case 3: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        update.x = static_cast<int32_t>(static_cast<uint32_t>(v));
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
        update.y = static_cast<int32_t>(static_cast<uint32_t>(v));
        return true;
      }
      case 5: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        update.width = static_cast<uint32_t>(v);
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
        update.height = static_cast<uint32_t>(v);
        return true;
      }
      case 7:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return readBytesIntoUpdate(r, update);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("OccupancyGridUpdate wire: decode failed"));
  }

  return update;
}

}  // namespace PJ
