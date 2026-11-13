// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/mesh_map_update_codec.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "mesh_channel_wire.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::MeshChannel;
using sdk::MeshChannelPatch;
using sdk::MeshMapUpdate;

bool readPatch(Reader& reader, std::vector<MeshChannelPatch>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  MeshChannelPatch patch;
  patch.channel = builtin_wire::wireDefaultMeshChannel();
  bool malformed = false;
  const bool parsed = parseFields(nested, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited || (tag.field != 1 && tag.field != 2)) {
      return false;
    }
    malformed |=
        tag.field == 1 ? !builtin_wire::readMeshChannel(r, patch.channel) : !builtin_wire::readSpan(r, patch.indices);
    return true;
  });
  // A patch without a named channel fails here, before validation, so a wire
  // of empty patches is not materialised first.
  if (!parsed || malformed || patch.channel.name.empty()) {
    return false;
  }
  out.push_back(std::move(patch));
  return true;
}

}  // namespace

Expected<void> validateMeshMapUpdate(const MeshMapUpdate& update) {
  std::set<std::pair<MeshChannel::Target, std::string_view>> seen;
  for (const MeshChannelPatch& patch : update.patches) {
    const MeshChannel& channel = patch.channel;
    if (patch.indices.size() % sizeof(uint32_t) != 0) {
      return unexpected("MeshMapUpdate: channel '" + channel.name + "' indices are not uint32");
    }
    if (!patch.indices.empty()) {
      if (auto ok =
              builtin_wire::validateMeshChannel(channel, patch.indices.size() / sizeof(uint32_t), "MeshMapUpdate");
          !ok) {
        return ok;
      }
    } else {
      // Dense: the element count is the base mesh's, checked when applied;
      // here the data need only hold whole elements.
      const uint64_t element_bytes = static_cast<uint64_t>(sdk::bytesPerElement(channel.datatype)) * channel.count;
      if (auto ok = builtin_wire::validateMeshChannel(
              channel, element_bytes == 0 ? 0 : channel.data.size() / element_bytes, "MeshMapUpdate");
          !ok) {
        return ok;
      }
    }
    if (!seen.emplace(channel.target, channel.name).second) {
      return unexpected("MeshMapUpdate: channel '" + channel.name + "' appears twice for the same target");
    }
  }
  return {};
}

std::vector<uint8_t> serializeMeshMapUpdate(const MeshMapUpdate& update) {
  std::vector<uint8_t> out;
  Writer writer(out);
  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, update.timestamp_ns); });
  writer.string(2, update.frame_id);
  writer.string(3, update.id);
  writer.string(4, update.geometry_id);
  for (const MeshChannelPatch& patch : update.patches) {
    writer.message(5, [&](Writer& nested) {
      nested.message(1, [&](Writer& channel) { builtin_wire::writeMeshChannel(channel, patch.channel); });
      nested.bytes(2, patch.indices.data(), patch.indices.size());
    });
  }
  return out;
}

Expected<MeshMapUpdate> deserializeMeshMapUpdate(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("MeshMapUpdate wire: empty buffer"));
  }
  auto owned = std::make_shared<std::vector<uint8_t>>(data, data + size);
  Reader reader(owned->data(), owned->size());
  MeshMapUpdate update;
  bool malformed = false;
  const bool parsed = parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        malformed |= !builtin_wire::readTimestampMessage(r, update.timestamp_ns);
        return true;
      case 2:
        malformed |= !r.readString(update.frame_id);
        return true;
      case 3:
        malformed |= !r.readString(update.id);
        return true;
      case 4:
        malformed |= !r.readString(update.geometry_id);
        return true;
      case 5:
        malformed |= !readPatch(r, update.patches);
        return true;
      default:
        return false;  // unknown field: skipped
    }
  });
  if (!parsed || malformed) {
    return unexpected(std::string("MeshMapUpdate wire: decode failed"));
  }
  update.anchor = std::move(owned);
  if (auto valid = validateMeshMapUpdate(update); !valid) {
    return unexpected(std::move(valid).error());
  }
  return update;
}

}  // namespace PJ
