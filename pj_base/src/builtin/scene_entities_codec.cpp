// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/scene_entities_codec.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::readPackedFixed32;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::ArrowPrimitive;
using sdk::AxesPrimitive;
using sdk::CubePrimitive;
using sdk::CylinderPrimitive;
using sdk::LinePrimitive;
using sdk::LineType;
using sdk::SceneEntities;
using sdk::SceneEntity;
using sdk::SpherePrimitive;
using sdk::TextPrimitive;
using sdk::TrianglePrimitive;

// ---------- LineType enum mapping ----------
//   proto: UNKNOWN=0, LINE_STRIP=1, LINE_LOOP=2, LINE_LIST=3
//   SDK:   kLineStrip=0, kLineLoop=1, kLineList=2

uint32_t lineTypeToWire(LineType type) {
  switch (type) {
    case LineType::kLineStrip:
      return 1;
    case LineType::kLineLoop:
      return 2;
    case LineType::kLineList:
      return 3;
  }
  return 1;
}

LineType lineTypeFromWire(uint64_t value) {
  switch (value) {
    case 2:
      return LineType::kLineLoop;
    case 3:
      return LineType::kLineList;
    case 0:
    case 1:
    default:
      return LineType::kLineStrip;
  }
}

// ---------- Color list helper (decoder accumulates) ----------

bool readColorIntoVector(Reader& reader, std::vector<sdk::ColorRGBA>& out) {
  sdk::ColorRGBA c;
  if (!builtin_wire::readColorMessage(reader, c)) {
    return false;
  }
  out.push_back(c);
  return true;
}

bool readPoint3IntoVector(Reader& reader, std::vector<sdk::Point3>& out) {
  sdk::Point3 p;
  if (!builtin_wire::readPoint3Message(reader, p)) {
    return false;
  }
  out.push_back(p);
  return true;
}

// ---------- ArrowPrimitive ----------

void writeArrowPrimitive(Writer& writer, const ArrowPrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.doubleField(2, p.shaft_length);
  writer.doubleField(3, p.shaft_diameter);
  writer.doubleField(4, p.head_length);
  writer.doubleField(5, p.head_diameter);
  writer.message(6, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
}

bool decodeArrowPrimitive(Reader& reader, ArrowPrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return tag.type == WireType::kFixed64 && r.readDouble(out.shaft_length);
      case 3:
        return tag.type == WireType::kFixed64 && r.readDouble(out.shaft_diameter);
      case 4:
        return tag.type == WireType::kFixed64 && r.readDouble(out.head_length);
      case 5:
        return tag.type == WireType::kFixed64 && r.readDouble(out.head_diameter);
      case 6:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readColorMessage(r, out.color);
      default:
        return false;
    }
  });
}

// ---------- CubePrimitive ----------

void writeCubePrimitive(Writer& writer, const CubePrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.message(2, [&](Writer& nested) { builtin_wire::writeVector3(nested, p.size); });
  writer.message(3, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
}

bool decodeCubePrimitive(Reader& reader, CubePrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return builtin_wire::readVector3Message(r, out.size);
      case 3:
        return builtin_wire::readColorMessage(r, out.color);
      default:
        return false;
    }
  });
}

// ---------- SpherePrimitive (same wire shape as Cube) ----------

void writeSpherePrimitive(Writer& writer, const SpherePrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.message(2, [&](Writer& nested) { builtin_wire::writeVector3(nested, p.size); });
  writer.message(3, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
}

bool decodeSpherePrimitive(Reader& reader, SpherePrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return builtin_wire::readVector3Message(r, out.size);
      case 3:
        return builtin_wire::readColorMessage(r, out.color);
      default:
        return false;
    }
  });
}

// ---------- CylinderPrimitive ----------

void writeCylinderPrimitive(Writer& writer, const CylinderPrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.message(2, [&](Writer& nested) { builtin_wire::writeVector3(nested, p.size); });
  writer.doubleField(3, p.bottom_scale);
  writer.doubleField(4, p.top_scale);
  writer.message(5, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
}

bool decodeCylinderPrimitive(Reader& reader, CylinderPrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readVector3Message(r, out.size);
      case 3:
        return tag.type == WireType::kFixed64 && r.readDouble(out.bottom_scale);
      case 4:
        return tag.type == WireType::kFixed64 && r.readDouble(out.top_scale);
      case 5:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readColorMessage(r, out.color);
      default:
        return false;
    }
  });
}

// ---------- LinePrimitive ----------

void writeLinePrimitive(Writer& writer, const LinePrimitive& p) {
  writer.varint(1, lineTypeToWire(p.type));
  writer.message(2, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.doubleField(3, p.thickness);
  writer.varint(4, p.scale_invariant ? 1u : 0u);
  for (const auto& point : p.points) {
    writer.message(5, [&](Writer& nested) { builtin_wire::writePoint3(nested, point); });
  }
  writer.message(6, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
  for (const auto& color : p.colors) {
    writer.message(7, [&](Writer& nested) { builtin_wire::writeColor(nested, color); });
  }
  writer.packedFixed32(8, p.indices);
}

bool decodeLinePrimitive(Reader& reader, LinePrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.type = lineTypeFromWire(v);
        return true;
      }
      case 2:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, out.pose);
      case 3:
        return tag.type == WireType::kFixed64 && r.readDouble(out.thickness);
      case 4: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.scale_invariant = (v != 0);
        return true;
      }
      case 5:
        return tag.type == WireType::kLengthDelimited && readPoint3IntoVector(r, out.points);
      case 6:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readColorMessage(r, out.color);
      case 7:
        return tag.type == WireType::kLengthDelimited && readColorIntoVector(r, out.colors);
      case 8:
        return tag.type == WireType::kLengthDelimited && readPackedFixed32(r, out.indices);
      default:
        return false;
    }
  });
}

// ---------- TrianglePrimitive ----------

void writeTrianglePrimitive(Writer& writer, const TrianglePrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  for (const auto& point : p.points) {
    writer.message(2, [&](Writer& nested) { builtin_wire::writePoint3(nested, point); });
  }
  writer.message(3, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
  for (const auto& color : p.colors) {
    writer.message(4, [&](Writer& nested) { builtin_wire::writeColor(nested, color); });
  }
  writer.packedFixed32(5, p.indices);
}

bool decodeTrianglePrimitive(Reader& reader, TrianglePrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return readPoint3IntoVector(r, out.points);
      case 3:
        return builtin_wire::readColorMessage(r, out.color);
      case 4:
        return readColorIntoVector(r, out.colors);
      case 5:
        return readPackedFixed32(r, out.indices);
      default:
        return false;
    }
  });
}

// ---------- TextPrimitive ----------

void writeTextPrimitive(Writer& writer, const TextPrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.varint(2, p.billboard ? 1u : 0u);
  writer.doubleField(3, p.font_size);
  writer.varint(4, p.scale_invariant ? 1u : 0u);
  writer.message(5, [&](Writer& nested) { builtin_wire::writeColor(nested, p.color); });
  writer.string(6, p.text);
}

bool decodeTextPrimitive(Reader& reader, TextPrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, out.pose);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.billboard = (v != 0);
        return true;
      }
      case 3:
        return tag.type == WireType::kFixed64 && r.readDouble(out.font_size);
      case 4: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.scale_invariant = (v != 0);
        return true;
      }
      case 5:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readColorMessage(r, out.color);
      case 6:
        return tag.type == WireType::kLengthDelimited && r.readString(out.text);
      default:
        return false;
    }
  });
}

// ---------- AxesPrimitive ----------

void writeAxesPrimitive(Writer& writer, const AxesPrimitive& p) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writePose(nested, p.pose); });
  writer.doubleField(2, p.length);
  writer.doubleField(3, p.thickness);
  writer.varint(4, p.scale_invariant ? 1u : 0u);
}

bool decodeAxesPrimitive(Reader& reader, AxesPrimitive& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readPoseMessage(r, out.pose);
      case 2:
        return tag.type == WireType::kFixed64 && r.readDouble(out.length);
      case 3:
        return tag.type == WireType::kFixed64 && r.readDouble(out.thickness);
      case 4: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.scale_invariant = (v != 0);
        return true;
      }
      default:
        return false;
    }
  });
}

// ---------- Nested-primitive read helpers ----------

template <typename Primitive, typename Decoder>
bool readPrimitiveIntoVector(Reader& reader, std::vector<Primitive>& out, Decoder&& decode) {
  Reader nested;
  if (!reader.readMessage(nested)) {
    return false;
  }
  Primitive primitive;
  if (!decode(nested, primitive)) {
    return false;
  }
  out.push_back(std::move(primitive));
  return true;
}

// ---------- SceneEntity ----------

void writeSceneEntity(Writer& writer, const SceneEntity& e) {
  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, e.timestamp); });
  writer.string(2, e.frame_id);
  writer.string(3, e.id);
  // Duration shares the seconds+nanos wire shape with Timestamp.
  writer.message(4, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, e.lifetime_ns); });
  writer.varint(5, e.frame_locked ? 1u : 0u);

  for (const auto& a : e.arrows) {
    writer.message(6, [&](Writer& nested) { writeArrowPrimitive(nested, a); });
  }
  for (const auto& c : e.cubes) {
    writer.message(7, [&](Writer& nested) { writeCubePrimitive(nested, c); });
  }
  for (const auto& s : e.spheres) {
    writer.message(8, [&](Writer& nested) { writeSpherePrimitive(nested, s); });
  }
  for (const auto& c : e.cylinders) {
    writer.message(9, [&](Writer& nested) { writeCylinderPrimitive(nested, c); });
  }
  for (const auto& l : e.lines) {
    writer.message(10, [&](Writer& nested) { writeLinePrimitive(nested, l); });
  }
  for (const auto& t : e.triangles) {
    writer.message(11, [&](Writer& nested) { writeTrianglePrimitive(nested, t); });
  }
  for (const auto& t : e.texts) {
    writer.message(12, [&](Writer& nested) { writeTextPrimitive(nested, t); });
  }
  for (const auto& a : e.axes) {
    writer.message(13, [&](Writer& nested) { writeAxesPrimitive(nested, a); });
  }
}

bool decodeSceneEntity(Reader& reader, SceneEntity& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, out.timestamp);
      case 2:
        return tag.type == WireType::kLengthDelimited && r.readString(out.frame_id);
      case 3:
        return tag.type == WireType::kLengthDelimited && r.readString(out.id);
      case 4:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, out.lifetime_ns);
      case 5: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        out.frame_locked = (v != 0);
        return true;
      }
      case 6:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.arrows, decodeArrowPrimitive);
      case 7:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.cubes, decodeCubePrimitive);
      case 8:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.spheres, decodeSpherePrimitive);
      case 9:
        return tag.type == WireType::kLengthDelimited &&
               readPrimitiveIntoVector(r, out.cylinders, decodeCylinderPrimitive);
      case 10:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.lines, decodeLinePrimitive);
      case 11:
        return tag.type == WireType::kLengthDelimited &&
               readPrimitiveIntoVector(r, out.triangles, decodeTrianglePrimitive);
      case 12:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.texts, decodeTextPrimitive);
      case 13:
        return tag.type == WireType::kLengthDelimited && readPrimitiveIntoVector(r, out.axes, decodeAxesPrimitive);
      default:
        return false;
    }
  });
}

}  // namespace

std::vector<uint8_t> serializeSceneEntities(const SceneEntities& entities) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& entity : entities.entities) {
    writer.message(1, [&](Writer& nested) { writeSceneEntity(nested, entity); });
  }

  return out;
}

Expected<sdk::SceneEntities> deserializeSceneEntities(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("SceneEntities wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::SceneEntities entities;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("SceneEntities wire: bad tag"));
    }

    if (tag.type != WireType::kLengthDelimited) {
      if (!reader.skip(tag.type)) {
        return unexpected(std::string("SceneEntities wire: skip failed"));
      }
      continue;
    }

    Reader nested;
    if (!reader.readMessage(nested)) {
      return unexpected(std::string("SceneEntities wire: bad nested message length"));
    }

    if (tag.field == 1) {
      SceneEntity entity;
      if (!decodeSceneEntity(nested, entity)) {
        return unexpected(std::string("SceneEntities wire: SceneEntity decode failed"));
      }
      entities.entities.push_back(std::move(entity));
    }
  }

  return entities;
}

}  // namespace PJ
