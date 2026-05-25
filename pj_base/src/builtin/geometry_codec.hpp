#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared across builtin-object codecs. Provides write /
// decode functions for the geometric primitives reused by multiple types
// (Vector3, Point3, Quaternion, Pose), for the canonical Timestamp encoding
// (proto seconds + nanos <-> SDK int64 nanoseconds), and for Color (proto
// double [0..1] RGBA <-> SDK ColorRGBA uint8 [0..255]).
//
// Inline-only; not exposed through the public include path.

#include <algorithm>
#include <cstdint>
#include <limits>

#include "pj_base/builtin/frame_transforms.hpp"   // Vector3, Quaternion, Pose
#include "pj_base/builtin/image_annotations.hpp"  // ColorRGBA
#include "pj_base/builtin/scene_entities.hpp"     // Point3
#include "pj_base/types.hpp"
#include "protobuf_wire.hpp"

namespace PJ::builtin_wire {

// ---------- Timestamp ----------

inline constexpr int64_t kNanosecondsPerSecond = 1000LL * 1000LL * 1000LL;

struct TimestampParts {
  int64_t seconds = 0;
  int32_t nanos = 0;
};

inline TimestampParts splitTimestamp(Timestamp timestamp_ns) {
  TimestampParts out;
  out.seconds = timestamp_ns / kNanosecondsPerSecond;
  out.nanos = static_cast<int32_t>(timestamp_ns % kNanosecondsPerSecond);
  if (out.nanos < 0) {
    --out.seconds;
    out.nanos += static_cast<int32_t>(kNanosecondsPerSecond);
  }
  return out;
}

inline bool combineTimestamp(const TimestampParts& parts, Timestamp& out) {
  if (parts.nanos < 0 || parts.nanos >= kNanosecondsPerSecond) {
    return false;
  }
  if (parts.seconds > std::numeric_limits<Timestamp>::max() / kNanosecondsPerSecond ||
      parts.seconds < std::numeric_limits<Timestamp>::min() / kNanosecondsPerSecond) {
    return false;
  }
  const Timestamp seconds_ns = parts.seconds * kNanosecondsPerSecond;
  if (seconds_ns > std::numeric_limits<Timestamp>::max() - parts.nanos) {
    return false;
  }
  out = seconds_ns + parts.nanos;
  return true;
}

inline void writeTimestamp(Writer& writer, Timestamp timestamp_ns) {
  const auto parts = splitTimestamp(timestamp_ns);
  writer.varint(1, static_cast<uint64_t>(parts.seconds));
  writer.varint(2, static_cast<uint32_t>(parts.nanos));
}

inline bool decodeTimestamp(Reader& reader, Timestamp& out) {
  TimestampParts parts;
  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kVarint) {
      return false;
    }
    uint64_t value = 0;
    if (!r.readVarint(value)) {
      return false;
    }
    if (tag.field == 1) {
      parts.seconds = static_cast<int64_t>(value);
      return true;
    }
    if (tag.field == 2) {
      parts.nanos = static_cast<int32_t>(value);
      return true;
    }
    return false;
  });
  return ok && combineTimestamp(parts, out);
}

inline bool readTimestampMessage(Reader& reader, Timestamp& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeTimestamp(nested, out);
}

// ---------- Vector3 ----------

inline void writeVector3(Writer& writer, const sdk::Vector3& v) {
  writer.doubleField(1, v.x);
  writer.doubleField(2, v.y);
  writer.doubleField(3, v.z);
}

inline bool decodeVector3(Reader& reader, sdk::Vector3& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kFixed64) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return r.readDouble(out.x);
      case 2:
        return r.readDouble(out.y);
      case 3:
        return r.readDouble(out.z);
      default:
        return false;
    }
  });
}

inline bool readVector3Message(Reader& reader, sdk::Vector3& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeVector3(nested, out);
}

// ---------- Point3 (same wire shape as Vector3, semantically distinct) ----------

inline void writePoint3(Writer& writer, const sdk::Point3& p) {
  writer.doubleField(1, p.x);
  writer.doubleField(2, p.y);
  writer.doubleField(3, p.z);
}

inline bool decodePoint3(Reader& reader, sdk::Point3& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kFixed64) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return r.readDouble(out.x);
      case 2:
        return r.readDouble(out.y);
      case 3:
        return r.readDouble(out.z);
      default:
        return false;
    }
  });
}

inline bool readPoint3Message(Reader& reader, sdk::Point3& out) {
  Reader nested;
  return reader.readMessage(nested) && decodePoint3(nested, out);
}

// ---------- Quaternion ----------

inline void writeQuaternion(Writer& writer, const sdk::Quaternion& q) {
  writer.doubleField(1, q.x);
  writer.doubleField(2, q.y);
  writer.doubleField(3, q.z);
  writer.doubleField(4, q.w);
}

inline bool decodeQuaternion(Reader& reader, sdk::Quaternion& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kFixed64) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return r.readDouble(out.x);
      case 2:
        return r.readDouble(out.y);
      case 3:
        return r.readDouble(out.z);
      case 4:
        return r.readDouble(out.w);
      default:
        return false;
    }
  });
}

inline bool readQuaternionMessage(Reader& reader, sdk::Quaternion& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeQuaternion(nested, out);
}

// ---------- Pose ----------

inline void writePose(Writer& writer, const sdk::Pose& p) {
  writer.message(1, [&](Writer& nested) { writeVector3(nested, p.position); });
  writer.message(2, [&](Writer& nested) { writeQuaternion(nested, p.orientation); });
}

inline bool decodePose(Reader& reader, sdk::Pose& out) {
  return parseFields(reader, [&](Tag tag, Reader& r) {
    if (tag.type != WireType::kLengthDelimited) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return readVector3Message(r, out.position);
      case 2:
        return readQuaternionMessage(r, out.orientation);
      default:
        return false;
    }
  });
}

inline bool readPoseMessage(Reader& reader, sdk::Pose& out) {
  Reader nested;
  return reader.readMessage(nested) && decodePose(nested, out);
}

// ---------- Color (uint8 RGBA <-> double 0..1) ----------

inline uint8_t normalizedToByte(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return static_cast<uint8_t>(value * 255.0 + 0.5);
}

inline void writeColor(Writer& writer, const sdk::ColorRGBA& color) {
  writer.doubleField(1, static_cast<double>(color.r) / 255.0);
  writer.doubleField(2, static_cast<double>(color.g) / 255.0);
  writer.doubleField(3, static_cast<double>(color.b) / 255.0);
  writer.doubleField(4, static_cast<double>(color.a) / 255.0);
}

inline bool decodeColor(Reader& reader, sdk::ColorRGBA& out) {
  double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
  const bool ok = parseFields(reader, [&](Tag tag, Reader& reader_) {
    if (tag.type != WireType::kFixed64) {
      return false;
    }
    switch (tag.field) {
      case 1:
        return reader_.readDouble(r);
      case 2:
        return reader_.readDouble(g);
      case 3:
        return reader_.readDouble(b);
      case 4:
        return reader_.readDouble(a);
      default:
        return false;
    }
  });
  if (!ok) {
    return false;
  }
  out = {normalizedToByte(r), normalizedToByte(g), normalizedToByte(b), normalizedToByte(a)};
  return true;
}

inline bool readColorMessage(Reader& reader, sdk::ColorRGBA& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeColor(nested, out);
}

}  // namespace PJ::builtin_wire
