// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/camera_info_codec.hpp"

#include <cstdint>
#include <string>
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
using sdk::CameraInfo;

}  // namespace

std::vector<uint8_t> serializeCameraInfo(const CameraInfo& info) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, info.timestamp_ns); });
  writer.string(2, info.frame_id);
  writer.varint(3, info.width);
  writer.varint(4, info.height);
  writer.string(5, info.distortion_model);
  writer.packedDouble(6, info.D);
  writer.packedDouble(7, info.K);
  writer.packedDouble(8, info.R);
  writer.packedDouble(9, info.P);

  return out;
}

Expected<sdk::CameraInfo> deserializeCameraInfo(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("CameraInfo wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::CameraInfo info;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, info.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(info.frame_id);
      case 3: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        info.width = static_cast<uint32_t>(v);
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
        info.height = static_cast<uint32_t>(v);
        return true;
      }
      case 5:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(info.distortion_model);
      case 6:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        // No clear(): readPackedDouble appends, so a `D` field split across
        // multiple packed chunks (valid proto, e.g. after message merge) is
        // preserved — matching the DepthImage decoder.
        return builtin_wire::readPackedDouble(r, info.D);
      case 7:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readPackedDoubleArray(r, info.K);
      case 8:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readPackedDoubleArray(r, info.R);
      case 9:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readPackedDoubleArray(r, info.P);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("CameraInfo wire: decode failed"));
  }

  return info;
}

}  // namespace PJ
