#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/** @file proto_reader.hpp @brief Bounds-checked protobuf wire reader. */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "pj_base/parser_module/core.hpp"

namespace pj {

class ProtoReader {
 public:
  static constexpr size_t kMaxRecursionDepth = 64;

  enum class WireType : uint8_t {
    kVarint = 0,
    kFixed64 = 1,
    kLengthDelimited = 2,
    kStartGroup = 3,
    kEndGroup = 4,
    kFixed32 = 5,
  };

  struct Field {
    uint32_t number = 0;
    WireType wire_type = WireType::kVarint;
    uint64_t integer = 0;
    ByteView bytes;
  };

  /// Nothrow-owned match collection. A lookup retains a bounded number of
  /// occurrences; larger valid messages report a resource error instead of
  /// exhausting guest memory.
  class FieldList {
   public:
    static constexpr size_t kMaximumFields = 64 * 1024;

    FieldList() = default;
    ~FieldList() {
      delete[] fields_;
    }
    FieldList(FieldList&& other) noexcept
        : fields_(std::exchange(other.fields_, nullptr)),
          size_(std::exchange(other.size_, 0)),
          capacity_(std::exchange(other.capacity_, 0)) {}
    FieldList& operator=(FieldList&& other) noexcept {
      if (this != &other) {
        delete[] fields_;
        fields_ = std::exchange(other.fields_, nullptr);
        size_ = std::exchange(other.size_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
      }
      return *this;
    }
    FieldList(const FieldList&) = delete;
    FieldList& operator=(const FieldList&) = delete;

    [[nodiscard]] bool empty() const noexcept {
      return size_ == 0;
    }
    [[nodiscard]] size_t size() const noexcept {
      return size_;
    }
    [[nodiscard]] const Field& back() const noexcept {
      return fields_[size_ - 1];
    }
    [[nodiscard]] const Field* begin() const noexcept {
      return fields_;
    }
    [[nodiscard]] const Field* end() const noexcept {
      return fields_ == nullptr ? nullptr : fields_ + size_;
    }

    [[nodiscard]] Status push(Field field) noexcept {
      if (size_ == kMaximumFields) {
        return Status::error("protobuf field match count exceeds the configured limit");
      }
      if (size_ == capacity_) {
        const size_t next_capacity = capacity_ == 0 ? size_t{8} : capacity_ * 2;
        const size_t bounded_capacity = next_capacity < kMaximumFields ? next_capacity : kMaximumFields;
        auto* grown = new (std::nothrow) Field[bounded_capacity];
        if (grown == nullptr) {
          return Status::error("allocation failed while collecting protobuf fields");
        }
        for (size_t index = 0; index < size_; ++index) {
          grown[index] = fields_[index];
        }
        delete[] fields_;
        fields_ = grown;
        capacity_ = bounded_capacity;
      }
      fields_[size_++] = field;
      return Status::ok();
    }

   private:
    Field* fields_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
  };

  explicit ProtoReader(ByteView message) : message_(message) {}

  [[nodiscard]] Expected<Field> last(uint32_t field_number) const {
    auto fields = matching(field_number);
    if (!fields) {
      return fields.status();
    }
    if (fields->empty()) {
      return Status::error("protobuf field is absent");
    }
    return fields->back();
  }

  [[nodiscard]] Expected<FieldList> matching(uint32_t field_number) const {
    if (field_number == 0 || field_number > UINT32_C(0x1FFFFFFF)) {
      return Status::error("protobuf field number is invalid");
    }
    FieldList result;
    size_t position = 0;
    while (position < message_.size) {
      auto field = nextField(position, depth_);
      if (!field) {
        return field.status();
      }
      if (field->number == field_number) {
        Status retained = result.push(*field);
        if (!retained.isOk()) {
          return retained;
        }
      }
    }
    return result;
  }

  [[nodiscard]] Expected<uint64_t> varint(uint32_t field_number) const {
    auto field = last(field_number);
    if (!field) {
      return field.status();
    }
    if (field->wire_type != WireType::kVarint) {
      return Status::error("protobuf field is not a varint");
    }
    return field->integer;
  }

  [[nodiscard]] Expected<int64_t> zigzag(uint32_t field_number) const {
    auto value = varint(field_number);
    if (!value) {
      return value.status();
    }
    const uint64_t sign = UINT64_C(0) - (*value & 1U);
    const uint64_t bits = (*value >> 1U) ^ sign;
    int64_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
  }

  [[nodiscard]] Expected<uint32_t> fixed32(uint32_t field_number) const {
    auto field = last(field_number);
    if (!field) {
      return field.status();
    }
    if (field->wire_type != WireType::kFixed32) {
      return Status::error("protobuf field is not fixed32");
    }
    return static_cast<uint32_t>(field->integer);
  }

  [[nodiscard]] Expected<uint64_t> fixed64(uint32_t field_number) const {
    auto field = last(field_number);
    if (!field) {
      return field.status();
    }
    if (field->wire_type != WireType::kFixed64) {
      return Status::error("protobuf field is not fixed64");
    }
    return field->integer;
  }

  [[nodiscard]] Expected<ByteView> bytes(uint32_t field_number) const {
    auto field = last(field_number);
    if (!field) {
      return field.status();
    }
    if (field->wire_type != WireType::kLengthDelimited) {
      return Status::error("protobuf field is not length-delimited");
    }
    return field->bytes;
  }

  [[nodiscard]] Expected<ProtoReader> submessage(uint32_t field_number) const {
    if (depth_ == kMaxRecursionDepth) {
      return Status::error("protobuf recursion depth exceeds 64");
    }
    auto value = bytes(field_number);
    if (!value) {
      return value.status();
    }
    return ProtoReader(*value, depth_ + 1);
  }

  /// Collect a repeated integer field. Both unpacked varints and packed
  /// length-delimited varint payloads are accepted in encounter order.
  [[nodiscard]] Expected<std::vector<uint64_t>> repeatedVarints(uint32_t field_number) const {
    auto fields = matching(field_number);
    if (!fields) {
      return fields.status();
    }
    PJ_PARSER_MODULE_TRY {
      std::vector<uint64_t> values;
      for (const auto& field : *fields) {
        if (field.wire_type == WireType::kVarint) {
          values.push_back(field.integer);
          continue;
        }
        if (field.wire_type != WireType::kLengthDelimited) {
          return Status::error("repeated protobuf integer uses an incompatible wire type");
        }
        size_t position = 0;
        while (position < field.bytes.size) {
          auto value = readVarint(field.bytes, position);
          if (!value) {
            return value.status();
          }
          values.push_back(*value);
        }
      }
      return values;
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while collecting repeated protobuf values");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while collecting repeated protobuf values");
    }
  }

  [[nodiscard]] Expected<std::vector<uint32_t>> repeatedFixed32(uint32_t field_number) const {
    return repeatedFixed<uint32_t>(field_number, WireType::kFixed32, 4);
  }

  [[nodiscard]] Expected<std::vector<uint64_t>> repeatedFixed64(uint32_t field_number) const {
    return repeatedFixed<uint64_t>(field_number, WireType::kFixed64, 8);
  }

 private:
  ProtoReader(ByteView message, size_t depth) : message_(message), depth_(depth) {}

  [[nodiscard]] static Expected<uint64_t> readVarint(ByteView bytes, size_t& position) {
    if (bytes.data == nullptr && bytes.size != 0) {
      return Status::error("protobuf input storage is null");
    }
    uint64_t value = 0;
    for (size_t byte_index = 0; byte_index < 10; ++byte_index) {
      if (position >= bytes.size) {
        return Status::error("truncated protobuf varint");
      }
      const uint8_t byte = bytes.data[position++];
      if (byte_index == 9 && (byte & UINT8_C(0xFE)) != 0) {
        return Status::error("protobuf varint overflows uint64");
      }
      value |= static_cast<uint64_t>(byte & UINT8_C(0x7F)) << (byte_index * 7);
      if ((byte & UINT8_C(0x80)) == 0) {
        return value;
      }
    }
    return Status::error("protobuf varint exceeds ten bytes");
  }

  [[nodiscard]] Expected<Field> nextField(size_t& position, size_t depth) const {
    auto key = readVarint(message_, position);
    if (!key) {
      return key.status();
    }
    const uint64_t raw_field_number = *key >> 3U;
    const uint32_t field_number = static_cast<uint32_t>(raw_field_number);
    const uint8_t wire_value = static_cast<uint8_t>(*key & 7U);
    if (raw_field_number == 0 || raw_field_number > UINT32_C(0x1FFFFFFF)) {
      return Status::error("protobuf field number is invalid");
    }
    if (wire_value > static_cast<uint8_t>(WireType::kFixed32) || wire_value == 6 || wire_value == 7) {
      return Status::error("protobuf wire type is invalid");
    }
    const auto wire_type = static_cast<WireType>(wire_value);
    Field field;
    field.number = field_number;
    field.wire_type = wire_type;
    switch (wire_type) {
      case WireType::kVarint: {
        auto value = readVarint(message_, position);
        if (!value) {
          return value.status();
        }
        field.integer = *value;
        return field;
      }
      case WireType::kFixed64:
        if (position > message_.size || 8 > message_.size - position) {
          return Status::error("truncated protobuf fixed64 field");
        }
        field.integer = readLittleEndian(message_.data + position, 8);
        position += 8;
        return field;
      case WireType::kLengthDelimited: {
        auto length = readVarint(message_, position);
        if (!length) {
          return length.status();
        }
        if (*length > std::numeric_limits<size_t>::max() || position > message_.size ||
            static_cast<size_t>(*length) > message_.size - position) {
          return Status::error("protobuf length-delimited field exceeds the remaining input");
        }
        field.bytes = ByteView(message_.data + position, static_cast<size_t>(*length));
        position += static_cast<size_t>(*length);
        return field;
      }
      case WireType::kStartGroup: {
        Status skipped = skipGroup(position, field_number, depth + 1);
        if (!skipped.isOk()) {
          return skipped;
        }
        return field;
      }
      case WireType::kEndGroup:
        return Status::error("unexpected protobuf end-group tag");
      case WireType::kFixed32:
        if (position > message_.size || 4 > message_.size - position) {
          return Status::error("truncated protobuf fixed32 field");
        }
        field.integer = readLittleEndian(message_.data + position, 4);
        position += 4;
        return field;
    }
    return Status::error("protobuf wire type is invalid");
  }

  [[nodiscard]] Status skipGroup(size_t& position, uint32_t group_number, size_t depth) const {
    if (depth > kMaxRecursionDepth) {
      return Status::error("protobuf recursion depth exceeds 64");
    }
    while (position < message_.size) {
      auto key = readVarint(message_, position);
      if (!key) {
        return key.status();
      }
      const uint64_t raw_field_number = *key >> 3U;
      const uint8_t wire_value = static_cast<uint8_t>(*key & 7U);
      if (raw_field_number == 0 || raw_field_number > UINT32_C(0x1FFFFFFF) || wire_value > 5 || wire_value == 6 ||
          wire_value == 7) {
        return Status::error("invalid protobuf field inside a group");
      }
      const uint32_t field_number = static_cast<uint32_t>(raw_field_number);
      const auto wire_type = static_cast<WireType>(wire_value);
      if (wire_type == WireType::kEndGroup) {
        return field_number == group_number ? Status::ok() : Status::error("protobuf group end tag does not match");
      }
      if (wire_type == WireType::kStartGroup) {
        Status nested = skipGroup(position, field_number, depth + 1);
        if (!nested.isOk()) {
          return nested;
        }
        continue;
      }
      Status skipped = skipValue(position, wire_type);
      if (!skipped.isOk()) {
        return skipped;
      }
    }
    return Status::error("truncated protobuf group");
  }

  [[nodiscard]] Status skipValue(size_t& position, WireType wire_type) const {
    if (wire_type == WireType::kVarint) {
      auto value = readVarint(message_, position);
      return value ? Status::ok() : value.status();
    }
    if (wire_type == WireType::kFixed64 || wire_type == WireType::kFixed32) {
      const size_t width = wire_type == WireType::kFixed64 ? 8 : 4;
      if (position > message_.size || width > message_.size - position) {
        return Status::error("truncated protobuf fixed field");
      }
      position += width;
      return Status::ok();
    }
    if (wire_type == WireType::kLengthDelimited) {
      auto length = readVarint(message_, position);
      if (!length || *length > std::numeric_limits<size_t>::max() || position > message_.size ||
          static_cast<size_t>(*length) > message_.size - position) {
        return Status::error("truncated protobuf length-delimited field");
      }
      position += static_cast<size_t>(*length);
      return Status::ok();
    }
    return Status::error("invalid protobuf group field");
  }

  [[nodiscard]] static uint64_t readLittleEndian(const uint8_t* data, size_t width) {
    uint64_t value = 0;
    for (size_t index = 0; index < width; ++index) {
      value |= static_cast<uint64_t>(data[index]) << (index * 8);
    }
    return value;
  }

  template <typename UInt>
  [[nodiscard]] Expected<std::vector<UInt>> repeatedFixed(
      uint32_t field_number, WireType unpacked_type, size_t width) const {
    auto fields = matching(field_number);
    if (!fields) {
      return fields.status();
    }
    PJ_PARSER_MODULE_TRY {
      std::vector<UInt> values;
      for (const auto& field : *fields) {
        if (field.wire_type == unpacked_type) {
          values.push_back(static_cast<UInt>(field.integer));
          continue;
        }
        if (field.wire_type != WireType::kLengthDelimited || field.bytes.size % width != 0) {
          return Status::error("repeated protobuf fixed field uses an incompatible wire encoding");
        }
        for (size_t offset = 0; offset < field.bytes.size; offset += width) {
          values.push_back(static_cast<UInt>(readLittleEndian(field.bytes.data + offset, width)));
        }
      }
      return values;
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while collecting repeated protobuf fixed values");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while collecting repeated protobuf fixed values");
    }
  }

  ByteView message_;
  size_t depth_ = 0;
};

}  // namespace pj
