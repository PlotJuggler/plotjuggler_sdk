// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/video_frame_codec.hpp"

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
using sdk::VideoFrame;

bool readBytesIntoFrame(Reader& reader, VideoFrame& out) {
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

std::vector<uint8_t> serializeVideoFrame(const VideoFrame& frame) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, frame.timestamp_ns); });
  writer.string(2, frame.frame_id);
  writer.string(3, frame.format);
  writer.bytes(4, frame.data.data(), frame.data.size());

  return out;
}

Expected<sdk::VideoFrame> deserializeVideoFrame(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("VideoFrame wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::VideoFrame frame;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, frame.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(frame.frame_id);
      case 3:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(frame.format);
      case 4:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return readBytesIntoFrame(r, frame);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("VideoFrame wire: decode failed"));
  }

  return frame;
}

}  // namespace PJ
