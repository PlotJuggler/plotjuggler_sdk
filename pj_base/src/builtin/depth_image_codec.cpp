// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/depth_image_codec.hpp"

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
using builtin_wire::readPackedDouble;
using builtin_wire::readPackedDoubleArray;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::DepthImage;

bool readBytesIntoDepth(Reader& reader, DepthImage& out) {
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

std::vector<uint8_t> serializeDepthImage(const DepthImage& depth) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, depth.timestamp_ns); });
  writer.varint(2, depth.width);
  writer.varint(3, depth.height);
  writer.string(4, depth.encoding);
  writer.bytes(5, depth.data.data(), depth.data.size());
  writer.packedDouble(6, depth.K);
  writer.string(7, depth.distortion_model);
  writer.packedDouble(8, depth.D);

  return out;
}

Expected<sdk::DepthImage> deserializeDepthImage(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("DepthImage wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::DepthImage depth;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, depth.timestamp_ns);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        depth.width = static_cast<uint32_t>(v);
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
        depth.height = static_cast<uint32_t>(v);
        return true;
      }
      case 4:
        return tag.type == WireType::kLengthDelimited && r.readString(depth.encoding);
      case 5:
        return tag.type == WireType::kLengthDelimited && readBytesIntoDepth(r, depth);
      case 6:
        return tag.type == WireType::kLengthDelimited && readPackedDoubleArray(r, depth.K);
      case 7:
        return tag.type == WireType::kLengthDelimited && r.readString(depth.distortion_model);
      case 8:
        return tag.type == WireType::kLengthDelimited && readPackedDouble(r, depth.D);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("DepthImage wire: decode failed"));
  }

  return depth;
}

}  // namespace PJ
