#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/builtin/frame_transforms.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace test_pb {

// Low-level protobuf wire builders. Mirror the encoding performed by the
// codecs themselves; tests use these to construct golden expected bytes
// independently of the codec under test.

inline void appendVarint(std::vector<uint8_t>& out, uint64_t v) {
  while (v >= 0x80u) {
    out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

inline void appendTag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
  appendVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

inline void appendFixed32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
  }
}

inline void appendFixed64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
  }
}

inline void appendFloat(std::vector<uint8_t>& out, float v) {
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(v));
  appendFixed32(out, bits);
}

inline void appendDouble(std::vector<uint8_t>& out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(v));
  appendFixed64(out, bits);
}

inline void appendLenDelim(std::vector<uint8_t>& out, const std::vector<uint8_t>& body) {
  appendVarint(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

inline void appendString(std::vector<uint8_t>& out, std::string_view value) {
  appendVarint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

inline void appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
  appendVarint(out, size);
  out.insert(out.end(), data, data + size);
}

// Geometry encoders matching `pj_base/src/builtin/geometry_codec.hpp` —
// each builds the inner message body (sans length prefix) for the proto type.

inline std::vector<uint8_t> encodeTimestamp(Timestamp timestamp_ns) {
  constexpr int64_t ns_per_second = 1'000'000'000LL;
  int64_t seconds = timestamp_ns / ns_per_second;
  int32_t nanos = static_cast<int32_t>(timestamp_ns % ns_per_second);
  if (nanos < 0) {
    --seconds;
    nanos += static_cast<int32_t>(ns_per_second);
  }
  std::vector<uint8_t> body;
  appendTag(body, 1, 0);
  appendVarint(body, static_cast<uint64_t>(seconds));
  appendTag(body, 2, 0);
  appendVarint(body, static_cast<uint32_t>(nanos));
  return body;
}

inline std::vector<uint8_t> encodeVector3(double x, double y, double z) {
  std::vector<uint8_t> body;
  appendTag(body, 1, 1);
  appendDouble(body, x);
  appendTag(body, 2, 1);
  appendDouble(body, y);
  appendTag(body, 3, 1);
  appendDouble(body, z);
  return body;
}

inline std::vector<uint8_t> encodeVector3(const sdk::Vector3& v) {
  return encodeVector3(v.x, v.y, v.z);
}

inline std::vector<uint8_t> encodeQuaternion(double x, double y, double z, double w) {
  std::vector<uint8_t> body;
  appendTag(body, 1, 1);
  appendDouble(body, x);
  appendTag(body, 2, 1);
  appendDouble(body, y);
  appendTag(body, 3, 1);
  appendDouble(body, z);
  appendTag(body, 4, 1);
  appendDouble(body, w);
  return body;
}

inline std::vector<uint8_t> encodeQuaternion(const sdk::Quaternion& q) {
  return encodeQuaternion(q.x, q.y, q.z, q.w);
}

inline std::vector<uint8_t> encodePose(const sdk::Pose& pose) {
  std::vector<uint8_t> body;
  appendTag(body, 1, 2);
  appendLenDelim(body, encodeVector3(pose.position));
  appendTag(body, 2, 2);
  appendLenDelim(body, encodeQuaternion(pose.orientation));
  return body;
}

}  // namespace test_pb
}  // namespace PJ
