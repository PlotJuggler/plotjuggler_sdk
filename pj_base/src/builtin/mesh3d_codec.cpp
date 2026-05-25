// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh3d_codec.hpp"

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
using sdk::Mesh3D;

bool readBytesIntoMesh(Reader& reader, Mesh3D& out) {
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

std::vector<uint8_t> serializeMesh3D(const Mesh3D& mesh) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, mesh.timestamp_ns); });
  writer.string(2, mesh.frame_id);
  writer.string(3, mesh.id);
  writer.message(4, [&](Writer& nested) { builtin_wire::writePose(nested, mesh.pose); });
  writer.message(5, [&](Writer& nested) { builtin_wire::writeVector3(nested, mesh.scale); });
  writer.string(6, mesh.format);
  writer.bytes(7, mesh.data.data(), mesh.data.size());
  writer.string(8, mesh.url);
  writer.message(9, [&](Writer& nested) { builtin_wire::writeColor(nested, mesh.color); });
  writer.varint(10, mesh.override_color ? 1u : 0u);

  return out;
}

Expected<sdk::Mesh3D> deserializeMesh3D(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("Mesh3D wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::Mesh3D mesh;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readTimestampMessage(r, mesh.timestamp_ns);
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(mesh.frame_id);
      case 3:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(mesh.id);
      case 4:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readPoseMessage(r, mesh.pose);
      case 5:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readVector3Message(r, mesh.scale);
      case 6:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(mesh.format);
      case 7:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return readBytesIntoMesh(r, mesh);
      case 8:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return r.readString(mesh.url);
      case 9:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        return builtin_wire::readColorMessage(r, mesh.color);
      case 10: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        mesh.override_color = (v != 0);
        return true;
      }
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("Mesh3D wire: decode failed"));
  }

  return mesh;
}

}  // namespace PJ
