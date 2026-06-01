// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

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
using sdk::BufferAnchor;
using sdk::VideoFrame;

// Reads the length-delimited `data` field (field 3) into an owning copy. The
// returned frame's `anchor` owns a fresh vector, so `data` stays valid past the
// lifetime of the wire buffer.
bool readBytesOwning(Reader& reader, VideoFrame& out) {
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

// Reads the length-delimited `data` field (field 3) as a non-owning view that
// ALIASES the wire buffer. The caller-supplied `anchor` keeps that buffer alive;
// no copy of the bitstream is made.
bool readBytesView(Reader& reader, const BufferAnchor& anchor, VideoFrame& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  out.data = Span<const uint8_t>(data, size);
  out.anchor = anchor;
  return true;
}

// Drives the shared field dispatch. `read_data` consumes the `data` field
// (field 3); the two deserialize entry points differ only in whether that
// callback copies or aliases the wire bytes. All other fields are identical.
template <typename ReadData>
bool parseVideoFrame(Reader& reader, VideoFrame& frame, ReadData&& read_data) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
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
        return read_data(r, frame);
      case 4:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(frame.format);
      default:
        return false;
    }
  });
}

}  // namespace

std::vector<uint8_t> serializeVideoFrame(const VideoFrame& frame) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, frame.timestamp_ns); });
  writer.string(2, frame.frame_id);
  writer.bytes(3, frame.data.data(), frame.data.size());
  writer.string(4, frame.format);

  return out;
}

Expected<sdk::VideoFrame> deserializeVideoFrame(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("VideoFrame wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::VideoFrame frame;

  const bool ok = parseVideoFrame(reader, frame, [](Reader& r, VideoFrame& f) { return readBytesOwning(r, f); });

  if (!ok) {
    return unexpected(std::string("VideoFrame wire: decode failed"));
  }

  return frame;
}

Expected<sdk::VideoFrame> deserializeVideoFrameView(const uint8_t* data, size_t size, sdk::BufferAnchor anchor) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("VideoFrame wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::VideoFrame frame;

  const bool ok = parseVideoFrame(reader, frame, [&](Reader& r, VideoFrame& f) { return readBytesView(r, anchor, f); });

  if (!ok) {
    return unexpected(std::string("VideoFrame wire: decode failed"));
  }

  return frame;
}

}  // namespace PJ
