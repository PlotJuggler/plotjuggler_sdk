// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_codec.hpp"

#include <cstdint>
#include <cstring>
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
using sdk::MeshFaceLink;
using sdk::MeshMap;
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

bool readBytesIntoMesh(Reader& reader, MeshMap& out) {
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

bool readLink(Reader& reader, std::vector<MeshFaceLink>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  MeshFaceLink link;
  const bool ok = parseFields(nested, [&](Tag tag, Reader& r) {
    uint32_t value = 0;
    if (!readVarint32(r, tag.type, value)) {
      return false;
    }
    switch (tag.field) {
      case 1:
        link.from_face = value;
        return true;
      case 2:
        link.to_face = value;
        return true;
      case 3:
        link.kind = value;
        return true;
      case 4:
        link.bidirectional = value != 0;
        return true;
      default:
        return false;
    }
  });
  if (!ok) {
    return false;
  }
  out.push_back(link);
  return true;
}

Expected<void> requirePosition(const std::vector<PointField>& fields, std::string_view axis) {
  const PointField* field = sdk::findField(fields, axis);
  if (field == nullptr) {
    return unexpected("MeshMap vertex lacks the required field '" + std::string(axis) + "'");
  }
  if ((field->datatype != PointField::Datatype::kFloat32 && field->datatype != PointField::Datatype::kFloat64) ||
      field->count != 1) {
    return unexpected("MeshMap vertex field '" + std::string(axis) + "' must be one float32 or float64");
  }
  return {};
}

}  // namespace

Expected<void> validateMeshMap(const MeshMap& mesh) {
  if (auto ok = builtin_wire::validateRecordFields(mesh.vertex_fields, mesh.vertex_stride, "MeshMap vertex"); !ok) {
    return ok;
  }
  if (auto ok = builtin_wire::validateRecordFields(mesh.face_fields, mesh.face_stride, "MeshMap face"); !ok) {
    return ok;
  }

  const uint64_t size = mesh.data.size();
  const uint64_t vertex_end = static_cast<uint64_t>(mesh.vertex_count) * mesh.vertex_stride;
  if (mesh.vertex_count > 0) {
    if (mesh.vertex_stride == 0) {
      return unexpected(std::string("MeshMap: zero vertex_stride with vertices declared"));
    }
    for (const char* axis : {"x", "y", "z"}) {
      if (auto ok = requirePosition(mesh.vertex_fields, axis); !ok) {
        return ok;
      }
    }
    if (vertex_end > size) {
      return unexpected(std::string("MeshMap: data shorter than vertex_count * vertex_stride"));
    }
  }

  if (mesh.face_count > 0) {
    if (mesh.face_stride == 0) {
      return unexpected(std::string("MeshMap: zero face_stride with faces declared"));
    }
    if (auto ok = builtin_wire::requireField(
            mesh.face_fields, sdk::kMeshMapVertexIndicesField, PointField::Datatype::kUint32, 3, "MeshMap face");
        !ok) {
      return ok;
    }
    if (mesh.face_data_offset < vertex_end) {
      return unexpected(std::string("MeshMap: face records overlap the vertex records"));
    }
    const uint64_t face_bytes = static_cast<uint64_t>(mesh.face_count) * mesh.face_stride;
    if (mesh.face_data_offset > size || face_bytes > size - mesh.face_data_offset) {
      return unexpected(std::string("MeshMap: data shorter than face_data_offset + face_count * face_stride"));
    }
    const uint32_t index_offset = sdk::findField(mesh.face_fields, sdk::kMeshMapVertexIndicesField)->offset;
    const uint8_t* face = mesh.data.data() + mesh.face_data_offset + index_offset;
    for (uint32_t f = 0; f < mesh.face_count; ++f, face += mesh.face_stride) {
      uint32_t indices[3];
      std::memcpy(indices, face, sizeof(indices));
      if (indices[0] >= mesh.vertex_count || indices[1] >= mesh.vertex_count || indices[2] >= mesh.vertex_count) {
        return unexpected("MeshMap: face " + std::to_string(f) + " references a vertex out of range");
      }
    }
  }

  for (const MeshFaceLink& link : mesh.links) {
    if (link.from_face >= mesh.face_count || link.to_face >= mesh.face_count) {
      return unexpected(std::string("MeshMap: a link references a face out of range"));
    }
  }
  return {};
}

std::vector<uint8_t> serializeMeshMap(const MeshMap& mesh) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, mesh.timestamp_ns); });
  writer.string(2, mesh.frame_id);
  writer.message(3, [&](Writer& nested) { builtin_wire::writePose(nested, mesh.origin); });
  writer.string(4, mesh.geometry_id);
  writer.varint(5, mesh.vertex_count);
  writer.varint(6, mesh.vertex_stride);
  for (const auto& field : mesh.vertex_fields) {
    writer.message(7, [&](Writer& nested) { builtin_wire::writePointField(nested, field); });
  }
  writer.varint(8, mesh.face_count);
  writer.varint(9, mesh.face_stride);
  writer.varint(10, mesh.face_data_offset);
  for (const auto& field : mesh.face_fields) {
    writer.message(11, [&](Writer& nested) { builtin_wire::writePointField(nested, field); });
  }
  for (const auto& link : mesh.links) {
    writer.message(12, [&](Writer& nested) {
      nested.varint(1, link.from_face);
      nested.varint(2, link.to_face);
      nested.varint(3, link.kind);
      nested.varint(4, link.bidirectional ? 1 : 0);
    });
  }
  writer.bytes(13, mesh.data.data(), mesh.data.size());
  return out;
}

Expected<MeshMap> deserializeMeshMap(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("MeshMap wire: empty buffer"));
  }

  Reader reader(data, size);
  MeshMap mesh;
  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, mesh.timestamp_ns);
      case 2:
        return tag.type == WireType::kLengthDelimited && r.readString(mesh.frame_id);
      case 3:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, mesh.origin);
      case 4:
        return tag.type == WireType::kLengthDelimited && r.readString(mesh.geometry_id);
      case 5:
        return readVarint32(r, tag.type, mesh.vertex_count);
      case 6:
        return readVarint32(r, tag.type, mesh.vertex_stride);
      case 7:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPointFieldIntoVector(r, mesh.vertex_fields);
      case 8:
        return readVarint32(r, tag.type, mesh.face_count);
      case 9:
        return readVarint32(r, tag.type, mesh.face_stride);
      case 10:
        return tag.type == WireType::kVarint && r.readVarint(mesh.face_data_offset);
      case 11:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPointFieldIntoVector(r, mesh.face_fields);
      case 12:
        return tag.type == WireType::kLengthDelimited && readLink(r, mesh.links);
      case 13:
        return tag.type == WireType::kLengthDelimited && readBytesIntoMesh(r, mesh);
      default:
        return false;
    }
  });
  if (!ok) {
    return unexpected(std::string("MeshMap wire: decode failed"));
  }
  if (auto valid = validateMeshMap(mesh); !valid) {
    return unexpected(std::move(valid).error());
  }
  return mesh;
}

}  // namespace PJ
