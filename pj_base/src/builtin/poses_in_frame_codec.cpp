// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/poses_in_frame_codec.hpp"

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

}  // namespace

std::vector<uint8_t> serializePosesInFrame(const sdk::PosesInFrame& poses) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, poses.timestamp_ns); });
  writer.string(2, poses.frame_id);
  for (const sdk::Pose& pose : poses.poses) {
    writer.message(3, [&](Writer& nested) { builtin_wire::writePose(nested, pose); });
  }

  return out;
}

Expected<sdk::PosesInFrame> deserializePosesInFrame(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("PosesInFrame wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::PosesInFrame result;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, result.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(result.frame_id);
      case 3: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        sdk::Pose pose;
        if (!builtin_wire::readPoseMessage(r, pose)) {
          return false;
        }
        result.poses.push_back(pose);
        return true;
      }
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("PosesInFrame wire: decode failed"));
  }

  return result;
}

}  // namespace PJ
