#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::builtin_wire {

enum class WireType : uint32_t {
  kVarint = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kFixed32 = 5,
};

struct Tag {
  uint32_t field = 0;
  WireType type = WireType::kVarint;
};

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>& out) : out_(out) {}

  void varint(uint32_t field, uint64_t value) {
    tag(field, WireType::kVarint);
    appendVarint(value);
  }

  void fixed64(uint32_t field, uint64_t value) {
    tag(field, WireType::kFixed64);
    appendFixed64(value);
  }

  void fixed32(uint32_t field, uint32_t value) {
    tag(field, WireType::kFixed32);
    appendFixed32(value);
  }

  /// Writes a packed array of fixed32 values (proto3 default packing for
  /// `repeated fixed32` scalar fields). Omits the field entirely when
  /// `values` is empty (matches proto3 default-field semantics).
  template <typename Range>
  void packedFixed32(uint32_t field, const Range& values) {
    const size_t count = static_cast<size_t>(values.size());
    if (count == 0) {
      return;
    }
    tag(field, WireType::kLengthDelimited);
    appendVarint(count * 4);
    for (uint32_t v : values) {
      appendFixed32(v);
    }
  }

  void doubleField(uint32_t field, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    fixed64(field, bits);
  }

  void floatField(uint32_t field, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(value));
    fixed32(field, bits);
  }

  /// Writes a packed array of double values (proto3 default packing for
  /// `repeated double` fields). Omits the field entirely when empty.
  template <typename Range>
  void packedDouble(uint32_t field, const Range& values) {
    const size_t count = static_cast<size_t>(values.size());
    if (count == 0) {
      return;
    }
    tag(field, WireType::kLengthDelimited);
    appendVarint(count * 8);
    for (double v : values) {
      uint64_t bits = 0;
      std::memcpy(&bits, &v, sizeof(v));
      appendFixed64(bits);
    }
  }

  void string(uint32_t field, std::string_view value) {
    bytes(field, reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

  void bytes(uint32_t field, const uint8_t* data, size_t size) {
    tag(field, WireType::kLengthDelimited);
    appendVarint(size);
    if (size != 0) {
      out_.insert(out_.end(), data, data + size);
    }
  }

  template <typename BuildMessage>
  void message(uint32_t field, BuildMessage&& build_message) {
    std::vector<uint8_t> body;
    Writer nested(body);
    build_message(nested);
    bytes(field, body.data(), body.size());
  }

 private:
  void tag(uint32_t field, WireType type) {
    appendVarint((static_cast<uint64_t>(field) << 3) | static_cast<uint32_t>(type));
  }

  void appendVarint(uint64_t value) {
    while (value >= 0x80u) {
      out_.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
      value >>= 7;
    }
    out_.push_back(static_cast<uint8_t>(value));
  }

  void appendFixed64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      out_.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
    }
  }

  void appendFixed32(uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      out_.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
    }
  }

  std::vector<uint8_t>& out_;
};

class Reader {
 public:
  Reader() = default;
  Reader(const uint8_t* data, size_t size) : p_(data), end_(data == nullptr ? nullptr : data + size) {}

  bool eof() const noexcept {
    return p_ == nullptr || p_ >= end_;
  }

  size_t remaining() const noexcept {
    if (p_ == nullptr) {
      return 0;
    }
    return static_cast<size_t>(end_ - p_);
  }

  bool readTag(Tag& out) {
    uint64_t raw = 0;
    if (!readVarint(raw) || raw == 0) {
      return false;
    }
    out.field = static_cast<uint32_t>(raw >> 3);
    out.type = static_cast<WireType>(raw & 0x7u);
    return out.field != 0;
  }

  bool readVarint(uint64_t& out) {
    out = 0;
    if (p_ == nullptr) {
      return false;
    }
    int shift = 0;
    for (int byte_index = 0; p_ < end_ && byte_index < 10; ++byte_index) {
      const uint8_t byte = *p_++;
      const uint64_t payload = static_cast<uint64_t>(byte & 0x7Fu);
      if (shift == 63 && payload > 1) {
        return false;
      }
      out |= payload << shift;
      if ((byte & 0x80u) == 0) {
        return true;
      }
      shift += 7;
    }
    return false;
  }

  bool readFixed64(uint64_t& out) {
    if (remaining() < 8) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p_[i]) << (8 * i);
    }
    p_ += 8;
    return true;
  }

  bool readFixed32(uint32_t& out) {
    if (remaining() < 4) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 4; ++i) {
      out |= static_cast<uint32_t>(p_[i]) << (8 * i);
    }
    p_ += 4;
    return true;
  }

  bool readDouble(double& out) {
    uint64_t bits = 0;
    if (!readFixed64(bits)) {
      return false;
    }
    std::memcpy(&out, &bits, sizeof(out));
    return true;
  }

  bool readFloat(float& out) {
    uint32_t bits = 0;
    if (!readFixed32(bits)) {
      return false;
    }
    std::memcpy(&out, &bits, sizeof(out));
    return true;
  }

  bool readString(std::string& out) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!readBytes(data, size)) {
      return false;
    }
    out.assign(reinterpret_cast<const char*>(data), size);
    return true;
  }

  /// Reads a length-delimited byte payload, returning a non-owning view into
  /// the underlying buffer. The view is valid only for the lifetime of the
  /// bytes the Reader was constructed over — callers that need to retain the
  /// data must copy it.
  bool readBytes(const uint8_t*& data, size_t& size) {
    return readBytesImpl(data, size);
  }

  bool readMessage(Reader& out) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!readBytes(data, size)) {
      return false;
    }
    out = Reader(data, size);
    return true;
  }

  bool skip(WireType type) {
    switch (type) {
      case WireType::kVarint: {
        uint64_t ignored = 0;
        return readVarint(ignored);
      }
      case WireType::kFixed64:
        return skipBytes(8);
      case WireType::kLengthDelimited: {
        const uint8_t* ignored = nullptr;
        size_t ignored_size = 0;
        return readBytes(ignored, ignored_size);
      }
      case WireType::kFixed32:
        return skipBytes(4);
      default:
        return false;
    }
  }

 private:
  bool readBytesImpl(const uint8_t*& data, size_t& size) {
    uint64_t len = 0;
    if (!readVarint(len) || len > remaining()) {
      return false;
    }
    data = p_;
    size = static_cast<size_t>(len);
    p_ += size;
    return true;
  }

  bool skipBytes(size_t size) {
    if (remaining() < size) {
      return false;
    }
    p_ += size;
    return true;
  }

  const uint8_t* p_ = nullptr;
  const uint8_t* end_ = nullptr;
};

/// Drives the standard tag-read / field-dispatch / skip-unknown loop used
/// by every per-message decoder. `handler(tag, reader)` consumes the field
/// and returns true when handled (including its own wire-type validation),
/// false to let the loop skip the field via reader.skip(). Returns false on
/// truncation / malformed input.
template <typename FieldHandler>
[[nodiscard]] inline bool parseFields(Reader& reader, FieldHandler&& handler) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }
    if (!handler(tag, reader) && !reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

/// Reads a proto3-packed `repeated fixed32` payload from a length-delimited
/// field into `out`. Values are appended; the caller may pre-clear if
/// needed. Returns false on truncation or non-multiple-of-4 length.
[[nodiscard]] inline bool readPackedFixed32(Reader& reader, std::vector<uint32_t>& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  if ((size % 4) != 0) {
    return false;
  }
  Reader sub(data, size);
  out.reserve(out.size() + size / 4);
  while (!sub.eof()) {
    uint32_t v = 0;
    if (!sub.readFixed32(v)) {
      return false;
    }
    out.push_back(v);
  }
  return true;
}

/// Reads a proto3-packed `repeated double` payload into `out`. Values are
/// appended; the caller may pre-clear if needed. Returns false on
/// truncation or non-multiple-of-8 length.
[[nodiscard]] inline bool readPackedDouble(Reader& reader, std::vector<double>& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  if ((size % 8) != 0) {
    return false;
  }
  Reader sub(data, size);
  out.reserve(out.size() + size / 8);
  while (!sub.eof()) {
    double v = 0.0;
    if (!sub.readDouble(v)) {
      return false;
    }
    out.push_back(v);
  }
  return true;
}

/// Reads a proto3-packed `repeated double` payload into a fixed-size array.
/// Fails if the wire length does not match exactly `N * 8` bytes.
template <size_t N>
[[nodiscard]] inline bool readPackedDoubleArray(Reader& reader, std::array<double, N>& out) {
  const uint8_t* data = nullptr;
  size_t size = 0;
  if (!reader.readBytes(data, size)) {
    return false;
  }
  if (size != N * 8) {
    return false;
  }
  Reader sub(data, size);
  for (size_t i = 0; i < N; ++i) {
    if (!sub.readDouble(out[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace PJ::builtin_wire
