// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_codec.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "mesh_channel_wire.hpp"
#include "point_field_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::MeshChannel;
using sdk::MeshFaceLink;
using sdk::MeshMap;
using sdk::PointField;

bool readLink(Reader& reader, std::vector<MeshFaceLink>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  MeshFaceLink link;
  bool malformed = false;
  const bool parsed = parseFields(nested, [&](Tag tag, Reader& r) {
    if (tag.field == 5) {
      if (tag.type != WireType::kFixed64) {
        return false;
      }
      malformed |= !r.readDouble(link.cost);
      return true;
    }
    if (tag.field < 1 || tag.field > 4 || tag.type != WireType::kVarint) {
      return false;  // unknown, or an unexpected wire type: skipped
    }
    uint64_t value = 0;
    malformed |= !r.readVarint(value);
    switch (tag.field) {
      case 1:
        malformed |= value > UINT32_MAX;
        link.from_face = static_cast<uint32_t>(value);
        break;
      case 2:
        malformed |= value > UINT32_MAX;
        link.to_face = static_cast<uint32_t>(value);
        break;
      case 3:
        malformed |= value > UINT32_MAX;
        link.kind = static_cast<uint32_t>(value);
        break;
      default:
        link.bidirectional = value != 0;
        break;
    }
    return true;
  });
  if (!parsed || malformed) {
    return false;
  }
  out.push_back(link);
  return true;
}

}  // namespace

Expected<void> validateMeshMap(const MeshMap& mesh) {
  const uint64_t position_bytes = sdk::bytesPerElement(mesh.position_type);
  if (mesh.position_type != PointField::Datatype::kFloat32 && mesh.position_type != PointField::Datatype::kFloat64) {
    return unexpected(std::string("MeshMap: position_type must be float32 or float64"));
  }
  if (static_cast<uint64_t>(mesh.positions.size()) != static_cast<uint64_t>(mesh.vertex_count) * 3 * position_bytes) {
    return unexpected(std::string("MeshMap: positions do not hold vertex_count xyz triplets"));
  }
  if (static_cast<uint64_t>(mesh.indices.size()) != static_cast<uint64_t>(mesh.face_count) * 3 * sizeof(uint32_t)) {
    return unexpected(std::string("MeshMap: indices do not hold face_count uint32 triplets"));
  }
  for (uint64_t i = 0; i < static_cast<uint64_t>(mesh.face_count) * 3; ++i) {
    uint32_t index = 0;
    std::memcpy(&index, mesh.indices.data() + (i * sizeof(uint32_t)), sizeof(index));
    if (index >= mesh.vertex_count) {
      return unexpected("MeshMap: face " + std::to_string(i / 3) + " references a vertex out of range");
    }
  }

  std::vector<const MeshChannel*> channels;
  channels.reserve(mesh.channels.size());
  for (const MeshChannel& channel : mesh.channels) {
    const uint64_t elements = channel.target == MeshChannel::Target::kVertex ? mesh.vertex_count : mesh.face_count;
    if (auto ok = builtin_wire::validateMeshChannel(channel, elements, "MeshMap"); !ok) {
      return ok;
    }
    channels.push_back(&channel);
  }
  if (auto ok = builtin_wire::requireUniqueChannels(channels, "MeshMap"); !ok) {
    return ok;
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
  writer.string(4, mesh.id);
  writer.string(5, mesh.geometry_id);
  writer.varint(6, mesh.vertex_count);
  writer.varint(7, builtin_wire::pointFieldDatatypeToWire(mesh.position_type));
  writer.bytes(8, mesh.positions.data(), mesh.positions.size());
  writer.varint(9, mesh.face_count);
  writer.bytes(10, mesh.indices.data(), mesh.indices.size());
  for (const MeshChannel& channel : mesh.channels) {
    writer.message(11, [&](Writer& nested) { builtin_wire::writeMeshChannel(nested, channel); });
  }
  for (const MeshFaceLink& link : mesh.links) {
    writer.message(12, [&](Writer& nested) {
      nested.varint(1, link.from_face);
      nested.varint(2, link.to_face);
      nested.varint(3, link.kind);
      nested.varint(4, link.bidirectional ? 1 : 0);
      if (!std::isnan(link.cost)) {
        nested.doubleField(5, link.cost);
      }
    });
  }
  return out;
}

Expected<MeshMap> deserializeMeshMap(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("MeshMap wire: empty buffer"));
  }
  // Parse an owned copy so every span can point straight into it.
  auto owned = std::make_shared<std::vector<uint8_t>>(data, data + size);
  Reader reader(owned->data(), owned->size());
  MeshMap mesh;
  bool malformed = false;

  const bool parsed = parseFields(reader, [&](Tag tag, Reader& r) {
    const bool length_delimited = tag.type == WireType::kLengthDelimited;
    uint64_t value = 0;
    switch (tag.field) {
      case 1:
        if (!length_delimited) {
          return false;
        }
        malformed |= !builtin_wire::readTimestampMessage(r, mesh.timestamp_ns);
        return true;
      case 2:
        if (!length_delimited) {
          return false;
        }
        malformed |= !r.readString(mesh.frame_id);
        return true;
      case 3:
        if (!length_delimited) {
          return false;
        }
        malformed |= !builtin_wire::readPoseMessage(r, mesh.origin);
        return true;
      case 4:
        if (!length_delimited) {
          return false;
        }
        malformed |= !r.readString(mesh.id);
        return true;
      case 5:
        if (!length_delimited) {
          return false;
        }
        malformed |= !r.readString(mesh.geometry_id);
        return true;
      case 6:
      case 7:
      case 9:
        if (tag.type != WireType::kVarint) {
          return false;
        }
        malformed |= !r.readVarint(value);
        if (tag.field == 7) {
          mesh.position_type = builtin_wire::pointFieldDatatypeFromWire(value);
        } else {
          malformed |= value > UINT32_MAX;
          (tag.field == 6 ? mesh.vertex_count : mesh.face_count) = static_cast<uint32_t>(value);
        }
        return true;
      case 8:
      case 10:
        if (!length_delimited) {
          return false;
        }
        malformed |= !builtin_wire::readSpan(r, tag.type, tag.field == 8 ? mesh.positions : mesh.indices);
        return true;
      case 11: {
        if (!length_delimited) {
          return false;
        }
        MeshChannel channel;
        malformed |= !builtin_wire::readMeshChannel(r, channel);
        mesh.channels.push_back(std::move(channel));
        return true;
      }
      case 12:
        if (!length_delimited) {
          return false;
        }
        malformed |= !readLink(r, mesh.links);
        return true;
      default:
        return false;  // unknown field: skipped, so later additions stay readable
    }
  });
  if (!parsed || malformed) {
    return unexpected(std::string("MeshMap wire: decode failed"));
  }
  mesh.anchor = std::move(owned);
  if (auto valid = validateMeshMap(mesh); !valid) {
    return unexpected(std::move(valid).error());
  }
  return mesh;
}

}  // namespace PJ
