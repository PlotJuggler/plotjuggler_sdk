// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_update_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "point_field_codec.hpp"
#include "protobuf_wire.hpp"
#include "record_layout.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::MeshMapUpdate;
using sdk::PointField;

bool readVarint32(Reader& reader, WireType type, uint32_t& out) {
  if (type != WireType::kVarint) {
    return false;
  }
  uint64_t v = 0;
  if (!reader.readVarint(v)) {
    return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

bool readTarget(Reader& reader, WireType type, MeshMapUpdate::Target& out) {
  uint32_t value = 0;
  if (!readVarint32(reader, type, value) || value > 1) {
    return false;
  }
  out = static_cast<MeshMapUpdate::Target>(value);
  return true;
}

bool readBytesIntoUpdate(Reader& reader, MeshMapUpdate& out) {
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

Expected<void> validateMeshMapUpdate(const MeshMapUpdate& update) {
  if (auto ok = builtin_wire::validateRecordFields(update.fields, update.stride, "MeshMapUpdate"); !ok) {
    return ok;
  }
  if (update.count > 0) {
    if (update.stride == 0) {
      return unexpected(std::string("MeshMapUpdate: zero stride with records declared"));
    }
    if (auto ok = builtin_wire::requireField(
            update.fields, sdk::kMeshMapUpdateIndexField, PointField::Datatype::kUint32, 1, "MeshMapUpdate");
        !ok) {
      return ok;
    }
  }
  if (static_cast<uint64_t>(update.data.size()) < static_cast<uint64_t>(update.count) * update.stride) {
    return unexpected(std::string("MeshMapUpdate: data shorter than count * stride"));
  }
  return {};
}

std::vector<uint8_t> serializeMeshMapUpdate(const MeshMapUpdate& update) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, update.timestamp_ns); });
  writer.string(2, update.frame_id);
  writer.string(3, update.geometry_id);
  writer.varint(4, static_cast<uint64_t>(update.target));
  writer.varint(5, update.count);
  writer.varint(6, update.stride);
  for (const auto& field : update.fields) {
    writer.message(7, [&](Writer& nested) { builtin_wire::writePointField(nested, field); });
  }
  writer.bytes(8, update.data.data(), update.data.size());
  return out;
}

Expected<MeshMapUpdate> deserializeMeshMapUpdate(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("MeshMapUpdate wire: empty buffer"));
  }

  Reader reader(data, size);
  MeshMapUpdate update;
  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, update.timestamp_ns);
      case 2:
        return tag.type == WireType::kLengthDelimited && r.readString(update.frame_id);
      case 3:
        return tag.type == WireType::kLengthDelimited && r.readString(update.geometry_id);
      case 4:
        return readTarget(r, tag.type, update.target);
      case 5:
        return readVarint32(r, tag.type, update.count);
      case 6:
        return readVarint32(r, tag.type, update.stride);
      case 7:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPointFieldIntoVector(r, update.fields);
      case 8:
        return tag.type == WireType::kLengthDelimited && readBytesIntoUpdate(r, update);
      default:
        return false;
    }
  });
  if (!ok) {
    return unexpected(std::string("MeshMapUpdate wire: decode failed"));
  }
  if (auto valid = validateMeshMapUpdate(update); !valid) {
    return unexpected(std::move(valid).error());
  }
  return update;
}

}  // namespace PJ
