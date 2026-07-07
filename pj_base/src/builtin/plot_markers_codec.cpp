// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/plot_markers_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::ColorRGBA;
using sdk::MarkerKind;
using sdk::MarkerProperty;
using sdk::MarkerSeverity;
using sdk::MarkerStatus;
using sdk::PlotMarker;
using sdk::PlotMarkers;

// ---- enum mapping (write raw value; read with default fallback) ----

MarkerKind mapKind(uint64_t v) {
  switch (v) {
    case 0:
      return MarkerKind::kRegion;
    case 1:
      return MarkerKind::kEvent;
    case 2:
      return MarkerKind::kValueBand;
    case 3:
      return MarkerKind::kLabel;
    default:
      return MarkerKind::kRegion;
  }
}

MarkerStatus mapStatus(uint64_t v) {
  switch (v) {
    case 1:
      return MarkerStatus::kPass;
    case 2:
      return MarkerStatus::kFail;
    case 0:
    default:
      return MarkerStatus::kNone;
  }
}

MarkerSeverity mapSeverity(uint64_t v) {
  switch (v) {
    case 1:
      return MarkerSeverity::kWarning;
    case 2:
      return MarkerSeverity::kError;
    case 3:
      return MarkerSeverity::kCritical;
    case 0:
    default:
      return MarkerSeverity::kInfo;
  }
}

uint8_t normalizedToByte(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return static_cast<uint8_t>(value * 255.0 + 0.5);
}

// ---- writers ----

void writeColor(Writer& writer, const ColorRGBA& color) {
  writer.doubleField(1, static_cast<double>(color.r) / 255.0);
  writer.doubleField(2, static_cast<double>(color.g) / 255.0);
  writer.doubleField(3, static_cast<double>(color.b) / 255.0);
  writer.doubleField(4, static_cast<double>(color.a) / 255.0);
}

void writeProperty(Writer& writer, const MarkerProperty& property) {
  writer.string(1, property.key);
  writer.string(2, property.value);
}

void writeMarker(Writer& writer, const PlotMarker& marker) {
  writer.varint(1, static_cast<uint64_t>(marker.kind));
  writer.varint(2, static_cast<uint64_t>(marker.t_start));
  writer.varint(3, static_cast<uint64_t>(marker.t_end));
  writer.doubleField(4, marker.value_low);
  writer.doubleField(5, marker.value_high);
  writer.varint(6, marker.has_value ? 1u : 0u);
  writer.varint(7, static_cast<uint64_t>(marker.status));
  writer.varint(8, static_cast<uint64_t>(marker.severity));
  writer.string(9, marker.category);
  writer.string(10, marker.label);
  writer.string(11, marker.description);
  writer.message(12, [&](Writer& nested) { writeColor(nested, marker.color); });
  for (const auto& property : marker.metadata) {
    writer.message(13, [&](Writer& nested) { writeProperty(nested, property); });
  }
}

// ---- readers ----

bool decodeColor(Reader& reader, ColorRGBA& out) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 0.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }
    if (tag.type == WireType::kFixed64 && tag.field >= 1 && tag.field <= 4) {
      double value = 0.0;
      if (!reader.readDouble(value)) {
        return false;
      }
      switch (tag.field) {
        case 1:
          r = value;
          break;
        case 2:
          g = value;
          break;
        case 3:
          b = value;
          break;
        case 4:
          a = value;
          break;
        default:
          break;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }

  out = {normalizedToByte(r), normalizedToByte(g), normalizedToByte(b), normalizedToByte(a)};
  return true;
}

bool decodeProperty(Reader& reader, MarkerProperty& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }
    if (tag.field == 1 && tag.type == WireType::kLengthDelimited) {
      if (!reader.readString(out.key)) {
        return false;
      }
    } else if (tag.field == 2 && tag.type == WireType::kLengthDelimited) {
      if (!reader.readString(out.value)) {
        return false;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeMarker(Reader& reader, PlotMarker& out) {
  // Handler returns true when it consumed the field, false to let parseFields skip
  // it (unknown field, or a known field with the wrong wire type).
  return parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.kind = mapKind(v);
        return true;
      }
      case 2: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.t_start = static_cast<Timestamp>(v);
        return true;
      }
      case 3: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.t_end = static_cast<Timestamp>(v);
        return true;
      }
      case 4:
        return tag.type == WireType::kFixed64 && r.readDouble(out.value_low);
      case 5:
        return tag.type == WireType::kFixed64 && r.readDouble(out.value_high);
      case 6: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.has_value = (v != 0);
        return true;
      }
      case 7: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.status = mapStatus(v);
        return true;
      }
      case 8: {
        uint64_t v = 0;
        if (tag.type != WireType::kVarint || !r.readVarint(v)) {
          return false;
        }
        out.severity = mapSeverity(v);
        return true;
      }
      case 9:
        return tag.type == WireType::kLengthDelimited && r.readString(out.category);
      case 10:
        return tag.type == WireType::kLengthDelimited && r.readString(out.label);
      case 11:
        return tag.type == WireType::kLengthDelimited && r.readString(out.description);
      case 12: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        Reader nested;
        return r.readMessage(nested) && decodeColor(nested, out.color);
      }
      case 13: {
        if (tag.type != WireType::kLengthDelimited) {
          return false;
        }
        Reader nested;
        MarkerProperty property;
        if (!r.readMessage(nested) || !decodeProperty(nested, property)) {
          return false;
        }
        out.metadata.push_back(std::move(property));
        return true;
      }
      default:
        return false;
    }
  });
}

}  // namespace

std::vector<uint8_t> serializePlotMarkers(const sdk::PlotMarkers& markers) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& marker : markers.markers) {
    writer.message(1, [&](Writer& nested) { writeMarker(nested, marker); });
  }

  return out;
}

Expected<sdk::PlotMarkers> deserializePlotMarkers(const uint8_t* data, size_t size) {
  // An empty buffer IS the canonical encoding of an empty set (proto semantics:
  // empty message = all defaults) — the "clear my markers" tombstone a producer
  // publishes, since the store is replace-only and markers are never deleted.
  if (size == 0) {
    return sdk::PlotMarkers{};
  }
  if (data == nullptr) {
    return unexpected(std::string("PlotMarkers wire: null buffer"));
  }

  Reader reader(data, size);
  sdk::PlotMarkers markers;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("PlotMarkers wire: bad tag"));
    }

    if (tag.field == 1 && tag.type == WireType::kLengthDelimited) {
      Reader nested;
      if (!reader.readMessage(nested)) {
        return unexpected(std::string("PlotMarkers wire: bad nested message length"));
      }
      PlotMarker marker;
      if (!decodeMarker(nested, marker)) {
        return unexpected(std::string("PlotMarkers wire: PlotMarker decode failed"));
      }
      markers.markers.push_back(std::move(marker));
    } else if (!reader.skip(tag.type)) {
      return unexpected(std::string("PlotMarkers wire: skip failed"));
    }
  }

  return markers;
}

}  // namespace PJ
