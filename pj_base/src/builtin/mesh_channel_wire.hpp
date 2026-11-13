#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Internal wire helpers for PJ.ChannelInfo (unit=1, no_data=2, kind=3,
// categories=4 {value=1, label=2, color=3}) and PJ.MeshChannel (name=1,
// target=2, datatype=3, count=4, data=5, info=6), shared by the MeshMap and
// MeshMapUpdate codecs, plus the checks both run on a channel.
//
// Decoding convention, as in protobuf parsers: an unknown field, or a known
// field number with the wrong wire type, is skipped (forward compatibility); a
// known field with the right wire type that fails to read or holds an invalid
// value fails the whole decode.
//
// Byte fields are returned as spans into the Reader's buffer: callers parse an
// owned copy of the wire so the spans stay valid under the object's anchor.
//
// Inline-only; not exposed through the public include path.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "pj_base/builtin/mesh_map.hpp"
#include "pj_base/expected.hpp"
#include "point_field_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ::builtin_wire {

inline bool readSpan(Reader& reader, Span<const uint8_t>& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  out = Span<const uint8_t>(data, size);
  return true;
}

// ---------- ChannelInfo ----------

inline void writeChannelInfo(Writer& writer, const sdk::ChannelInfo& info) {
  writer.string(1, info.unit);
  if (info.no_data.has_value()) {
    writer.doubleField(2, *info.no_data);
  }
  writer.varint(3, static_cast<uint64_t>(info.kind));
  for (const auto& category : info.categories) {
    writer.message(4, [&](Writer& nested) {
      nested.varint(1, static_cast<uint64_t>(category.value));
      nested.string(2, category.label);
      if (category.color.has_value()) {
        nested.message(3, [&](Writer& color) { writeColor(color, *category.color); });
      }
    });
  }
}

inline bool readCategory(Reader& reader, std::vector<sdk::ChannelCategory>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  sdk::ChannelCategory category;
  bool malformed = false;
  const bool parsed = parseFields(nested, [&](Tag tag, Reader& r) {
    // A known field with the expected wire type is consumed here; a bad value
    // marks the message malformed instead of letting parseFields re-skip it.
    switch (tag.field) {
      case 1: {
        uint64_t value = 0;
        if (tag.type != WireType::kVarint) {
          return false;
        }
        malformed |= !r.readVarint(value);
        category.value = static_cast<int64_t>(value);
        return true;
      }
      case 2:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !r.readString(category.label);
        return true;
      case 3: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        sdk::ColorRGBA color;
        malformed |= !readColorMessage(r, color);
        category.color = color;
        return true;
      }
      default:
        return false;  // unknown field: skipped by parseFields
    }
  });
  if (!parsed || malformed) {
    return false;
  }
  out.push_back(std::move(category));
  return true;
}

inline bool readChannelInfo(Reader& reader, sdk::ChannelInfo& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  bool malformed = false;
  const bool parsed = parseFields(nested, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !r.readString(out.unit);
        return true;
      case 2: {
        if (tag.type != WireType::kFixed64) {
          return false;
        }
        double value = 0.0;
        malformed |= !r.readDouble(value);
        out.no_data = value;
        return true;
      }
      case 3: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t value = 0;
        malformed |= !r.readVarint(value);
        // A kind this SDK does not know reads as continuous, the safe default.
        out.kind = value <= 2 ? static_cast<sdk::ChannelKind>(value) : sdk::ChannelKind::kContinuous;
        return true;
      }
      case 4:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !readCategory(r, out.categories);
        return true;
      default:
        return false;
    }
  });
  return parsed && !malformed;
}

// ---------- MeshChannel ----------

inline void writeMeshChannel(Writer& writer, const sdk::MeshChannel& channel) {
  writer.string(1, channel.name);
  writer.varint(2, static_cast<uint64_t>(channel.target));
  writer.varint(3, pointFieldDatatypeToWire(channel.datatype));
  writer.varint(4, channel.count);
  writer.bytes(5, channel.data.data(), channel.data.size());
  writer.message(6, [&](Writer& nested) { writeChannelInfo(nested, channel.info); });
}

/// A MeshChannel holding the proto3 zero values (VERTEX, UNKNOWN datatype,
/// count 0). A standard encoder omits fields equal to them, so a decoder starts
/// from this rather than from the struct defaults, which differ.
inline sdk::MeshChannel wireDefaultMeshChannel() {
  sdk::MeshChannel channel;
  channel.target = sdk::MeshChannel::Target::kVertex;
  channel.datatype = sdk::PointField::Datatype::kUnknown;
  channel.count = 0;
  return channel;
}

/// Reads one PJ.MeshChannel submessage, overwriting only the fields present on
/// the wire: start `out` from wireDefaultMeshChannel().
inline bool readMeshChannel(Reader& reader, sdk::MeshChannel& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  bool malformed = false;
  const bool parsed = parseFields(nested, [&](Tag tag, Reader& r) {
    uint64_t value = 0;
    switch (tag.field) {
      case 1:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !r.readString(out.name);
        return true;
      case 2:
        if (tag.type != WireType::kVarint) {
          return false;
        }
        malformed |= !r.readVarint(value) || value > 1;
        out.target = static_cast<sdk::MeshChannel::Target>(value & 1U);
        return true;
      case 3:
        if (tag.type != WireType::kVarint) {
          return false;
        }
        malformed |= !r.readVarint(value);
        out.datatype = pointFieldDatatypeFromWire(value);
        return true;
      case 4:
        if (tag.type != WireType::kVarint) {
          return false;
        }
        malformed |= !r.readVarint(value) || value > UINT32_MAX;
        out.count = static_cast<uint32_t>(value);
        return true;
      case 5:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !readSpan(r, out.data);
        return true;
      case 6:
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        malformed |= !readChannelInfo(r, out.info);
        return true;
      default:
        return false;
    }
  });
  return parsed && !malformed;
}

// ---------- checks ----------

/// A channel's own layout: a name, a known datatype, a non-zero count, and
/// `data` holding exactly `elements` elements. `what` prefixes the error.
[[nodiscard]] inline Expected<void> validateMeshChannel(
    const sdk::MeshChannel& channel, uint64_t elements, const std::string& what) {
  if (channel.name.empty()) {
    return unexpected(what + ": a channel has no name");
  }
  const uint64_t element_bytes = sdk::bytesPerElement(channel.datatype);
  if (element_bytes == 0) {
    return unexpected(what + ": channel '" + channel.name + "' has an unknown datatype");
  }
  if (channel.count == 0) {
    return unexpected(what + ": channel '" + channel.name + "' has a zero count");
  }
  if (elements > UINT64_MAX / channel.count / element_bytes ||
      static_cast<uint64_t>(channel.data.size()) != elements * channel.count * element_bytes) {
    return unexpected(what + ": channel '" + channel.name + "' data does not match its element count");
  }
  return {};
}

}  // namespace PJ::builtin_wire
