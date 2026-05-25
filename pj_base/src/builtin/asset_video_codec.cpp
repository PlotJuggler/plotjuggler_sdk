// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/asset_video_codec.hpp"

#include <cstdint>
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
using sdk::AssetVideo;

}  // namespace

std::vector<uint8_t> serializeAssetVideo(const AssetVideo& asset) {
  std::vector<uint8_t> out;
  Writer writer(out);

  // Both `time_origin` and `duration` use the seconds+nanos wire shape;
  // omit the field entirely when the SDK optional is empty.
  if (asset.time_origin_ns.has_value()) {
    writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, *asset.time_origin_ns); });
  }
  if (asset.duration_ns.has_value()) {
    writer.message(2, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, *asset.duration_ns); });
  }
  writer.string(3, asset.file_path);
  writer.string(4, asset.media_type);
  writer.varint(5, asset.width);
  writer.varint(6, asset.height);
  writer.doubleField(7, asset.frame_rate);

  return out;
}

Expected<sdk::AssetVideo> deserializeAssetVideo(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("AssetVideo wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::AssetVideo asset;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        Timestamp t = 0;
        if (!builtin_wire::readTimestampMessage(r, t)) {
          return false;
        }
        asset.time_origin_ns = t;
        return true;
      }
      case 2: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        Timestamp d = 0;
        if (!builtin_wire::readTimestampMessage(r, d)) {
          return false;
        }
        asset.duration_ns = d;
        return true;
      }
      case 3:
        return tag.type == WireType::kLengthDelimited && r.readString(asset.file_path);
      case 4:
        return tag.type == WireType::kLengthDelimited && r.readString(asset.media_type);
      case 5: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        asset.width = static_cast<uint32_t>(v);
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
        asset.height = static_cast<uint32_t>(v);
        return true;
      }
      case 7:
        return tag.type == WireType::kFixed64 && r.readDouble(asset.frame_rate);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("AssetVideo wire: decode failed"));
  }

  return asset;
}

}  // namespace PJ
