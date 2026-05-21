// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/frame_transforms_codec.hpp"

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
using sdk::FrameTransform;
using sdk::FrameTransforms;

void writeFrameTransform(Writer& writer, const FrameTransform& transform) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, transform.timestamp); });
  writer.string(2, transform.parent_frame_id);
  writer.string(3, transform.child_frame_id);
  writer.message(4, [&](Writer& nested) { builtin_wire::writeVector3(nested, transform.translation); });
  writer.message(5, [&](Writer& nested) { builtin_wire::writeQuaternion(nested, transform.rotation); });
}

bool decodeFrameTransform(Reader& reader, FrameTransform& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return builtin_wire::readTimestampMessage(r, out.timestamp);
      case 2:
        return r.readString(out.parent_frame_id);
      case 3:
        return r.readString(out.child_frame_id);
      case 4:
        return builtin_wire::readVector3Message(r, out.translation);
      case 5:
        return builtin_wire::readQuaternionMessage(r, out.rotation);
      default:
        return false;
    }
  });
}

}  // namespace

std::vector<uint8_t> serializeFrameTransforms(const FrameTransforms& transforms) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& transform : transforms.transforms) {
    writer.message(1, [&](Writer& nested) { writeFrameTransform(nested, transform); });
  }

  return out;
}

Expected<sdk::FrameTransforms> deserializeFrameTransforms(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("FrameTransforms wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::FrameTransforms transforms;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("FrameTransforms wire: bad tag"));
    }

    if (tag.type != WireType::kLengthDelimited) {
      if (!reader.skip(tag.type)) {
        return unexpected(std::string("FrameTransforms wire: skip failed"));
      }
      continue;
    }

    Reader nested;
    if (!reader.readMessage(nested)) {
      return unexpected(std::string("FrameTransforms wire: bad nested message length"));
    }

    if (tag.field == 1) {
      FrameTransform transform;
      if (!decodeFrameTransform(nested, transform)) {
        return unexpected(std::string("FrameTransforms wire: FrameTransform decode failed"));
      }
      transforms.transforms.push_back(std::move(transform));
    }
  }

  return transforms;
}

}  // namespace PJ
