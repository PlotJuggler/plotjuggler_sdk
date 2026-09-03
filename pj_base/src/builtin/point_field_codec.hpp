#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Internal wire helpers for the PJ.PointField message (name=1, offset=2,
// datatype=3, count=4), shared by every codec that carries the per-record
// channel model: PointCloud, VoxelGrid, GridMap.
//
// Inline-only; not exposed through the public include path.

#include <cstdint>
#include <utility>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "protobuf_wire.hpp"

namespace PJ::builtin_wire {

// The proto enum (UNKNOWN=0, INT8=1, UINT8=2, INT16=3, UINT16=4, INT32=5,
// UINT32=6, FLOAT32=7, FLOAT64=8) is numerically identical to
// sdk::PointField::Datatype, so encoding is a cast; decoding still funnels
// unknown numbers to kUnknown instead of trusting the wire.

inline uint32_t pointFieldDatatypeToWire(sdk::PointField::Datatype dt) {
  return static_cast<uint32_t>(dt);
}

inline sdk::PointField::Datatype pointFieldDatatypeFromWire(uint64_t value) {
  using Datatype = sdk::PointField::Datatype;
  switch (value) {
    case 1:
      return Datatype::kInt8;
    case 2:
      return Datatype::kUint8;
    case 3:
      return Datatype::kInt16;
    case 4:
      return Datatype::kUint16;
    case 5:
      return Datatype::kInt32;
    case 6:
      return Datatype::kUint32;
    case 7:
      return Datatype::kFloat32;
    case 8:
      return Datatype::kFloat64;
    case 0:
    default:
      return Datatype::kUnknown;
  }
}

inline void writePointField(Writer& writer, const sdk::PointField& field) {
  writer.string(1, field.name);
  writer.varint(2, field.offset);
  writer.varint(3, pointFieldDatatypeToWire(field.datatype));
  writer.varint(4, field.count);
}

inline bool decodePointField(Reader& reader, sdk::PointField& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.field == 1) {
      return tag.type == WireType::kLengthDelimited && r.readString(out.name);
    }
    if (tag.type != WireType::kVarint) {
      return false;
    }
    uint64_t v = 0;
    if (!r.readVarint(v)) {
      return false;
    }
    switch (tag.field) {
      case 2:
        out.offset = static_cast<uint32_t>(v);
        return true;
      case 3:
        out.datatype = pointFieldDatatypeFromWire(v);
        return true;
      case 4:
        out.count = static_cast<uint32_t>(v);
        return true;
      default:
        return false;
    }
  });
}

inline bool readPointFieldIntoVector(Reader& reader, std::vector<sdk::PointField>& out) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  sdk::PointField field;
  if (!decodePointField(nested, field)) {
    return false;
  }
  out.push_back(std::move(field));
  return true;
}

}  // namespace PJ::builtin_wire
