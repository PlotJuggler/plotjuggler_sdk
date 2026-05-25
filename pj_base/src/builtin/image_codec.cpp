// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/image_codec.hpp"

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
using sdk::Image;

bool readBytesIntoImage(Reader& reader, Image& out) {
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

std::vector<uint8_t> serializeImage(const Image& image) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, image.timestamp_ns); });
  writer.varint(2, image.width);
  writer.varint(3, image.height);
  writer.string(4, image.encoding);
  writer.varint(5, image.row_step);
  writer.varint(6, image.is_bigendian ? 1u : 0u);
  writer.bytes(7, image.data.data(), image.data.size());
  if (image.compressed_depth_min.has_value()) {
    writer.floatField(8, *image.compressed_depth_min);
  }
  if (image.compressed_depth_max.has_value()) {
    writer.floatField(9, *image.compressed_depth_max);
  }

  return out;
}

Expected<sdk::Image> deserializeImage(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("Image wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::Image image;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, image.timestamp_ns);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        image.width = static_cast<uint32_t>(v);
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
        image.height = static_cast<uint32_t>(v);
        return true;
      }
      case 4:
        return tag.type == WireType::kLengthDelimited && r.readString(image.encoding);
      case 5: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        image.row_step = static_cast<uint32_t>(v);
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
        image.is_bigendian = (v != 0);
        return true;
      }
      case 7:
        return tag.type == WireType::kLengthDelimited && readBytesIntoImage(r, image);
      case 8: {
        if (tag.type != WireType::kFixed32) {
          return false;
        }
        float v = 0.0f;
        if (!r.readFloat(v)) {
          return false;
        }
        image.compressed_depth_min = v;
        return true;
      }
      case 9: {
        if (tag.type != WireType::kFixed32) {
          return false;
        }
        float v = 0.0f;
        if (!r.readFloat(v)) {
          return false;
        }
        image.compressed_depth_max = v;
        return true;
      }
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("Image wire: decode failed"));
  }

  return image;
}

}  // namespace PJ
