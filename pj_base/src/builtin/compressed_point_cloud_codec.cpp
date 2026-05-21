// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_base/builtin/compressed_point_cloud_codec.hpp"

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
using sdk::CompressedPointCloud;

bool readBytesIntoCloud(Reader& reader, CompressedPointCloud& out) {
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

std::vector<uint8_t> serializeCompressedPointCloud(const CompressedPointCloud& cloud) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, cloud.timestamp_ns); });
  writer.string(2, cloud.frame_id);
  writer.string(3, cloud.format);
  writer.bytes(4, cloud.data.data(), cloud.data.size());

  return out;
}

Expected<sdk::CompressedPointCloud> deserializeCompressedPointCloud(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("CompressedPointCloud wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::CompressedPointCloud cloud;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return builtin_wire::readTimestampMessage(r, cloud.timestamp_ns);
      case 2:
        return r.readString(cloud.frame_id);
      case 3:
        return r.readString(cloud.format);
      case 4:
        return readBytesIntoCloud(r, cloud);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("CompressedPointCloud wire: decode failed"));
  }

  return cloud;
}

}  // namespace PJ
