// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/point_cloud_codec.hpp"

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
using builtin_wire::readPointFieldIntoVector;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::writePointField;
using builtin_wire::Writer;
using sdk::PointCloud;
using sdk::PointField;

// ---------- PointCloud payload bytes ----------

bool readBytesIntoCloud(Reader& reader, PointCloud& out) {
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

std::vector<uint8_t> serializePointCloud(const PointCloud& cloud) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, cloud.timestamp_ns); });
  writer.varint(2, cloud.width);
  writer.varint(3, cloud.height);
  writer.varint(4, cloud.point_step);
  writer.varint(5, cloud.row_step);
  writer.varint(6, cloud.is_bigendian ? 1u : 0u);
  writer.varint(7, cloud.is_dense ? 1u : 0u);
  for (const auto& field : cloud.fields) {
    writer.message(8, [&](Writer& nested) { writePointField(nested, field); });
  }
  writer.bytes(9, cloud.data.data(), cloud.data.size());
  writer.string(10, cloud.frame_id);

  return out;
}

Expected<sdk::PointCloud> deserializePointCloud(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("PointCloud wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::PointCloud cloud;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, cloud.timestamp_ns);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        cloud.width = static_cast<uint32_t>(v);
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
        cloud.height = static_cast<uint32_t>(v);
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
        cloud.point_step = static_cast<uint32_t>(v);
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
        cloud.row_step = static_cast<uint32_t>(v);
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
        cloud.is_bigendian = (v != 0);
        return true;
      }
      case 7: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        cloud.is_dense = (v != 0);
        return true;
      }
      case 8:
        return tag.type == WireType::kLengthDelimited && readPointFieldIntoVector(r, cloud.fields);
      case 9:
        return tag.type == WireType::kLengthDelimited && readBytesIntoCloud(r, cloud);
      case 10:
        return tag.type == WireType::kLengthDelimited && r.readString(cloud.frame_id);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("PointCloud wire: decode failed"));
  }

  return cloud;
}

}  // namespace PJ
