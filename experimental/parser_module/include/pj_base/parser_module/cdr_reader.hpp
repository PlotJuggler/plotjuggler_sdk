#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cdr_reader.hpp
 * @brief Bounds-checked XCDR1 reader with optional compiled field traversal.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include "pj_base/parser_module/core.hpp"

namespace pj {

class CdrTraversalPlan;
using CdrFieldId = size_t;

class CdrReader {
 public:
  static constexpr size_t kMaxTraversalDepth = 64;

  explicit CdrReader(PayloadView payload) : payload_(payload) {
    initialize();
  }

  CdrReader(PayloadView payload, const CdrTraversalPlan& plan);

  [[nodiscard]] const Status& status() const noexcept {
    return status_;
  }

  [[nodiscard]] bool littleEndian() const noexcept {
    return little_endian_;
  }

  [[nodiscard]] size_t position() const noexcept {
    return position_;
  }

  [[nodiscard]] size_t traversalCount() const noexcept {
    return traversal_count_;
  }

  [[nodiscard]] Status align(size_t alignment) {
    if (!status_.isOk()) {
      return status_;
    }
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      return fail("CDR alignment must be a nonzero power of two");
    }
    const size_t relative = position_ - data_origin_;
    const size_t padding = (alignment - (relative & (alignment - 1))) & (alignment - 1);
    if (padding > remaining()) {
      return fail("truncated CDR alignment padding");
    }
    position_ += padding;
    return Status::ok();
  }

  [[nodiscard]] Expected<bool> readBool() {
    auto value = readU8();
    if (!value) {
      return value.status();
    }
    if (*value > 1) {
      return fail("CDR bool is not 0 or 1");
    }
    return *value != 0;
  }

  [[nodiscard]] Expected<int8_t> readI8() {
    return readSigned<int8_t, uint8_t>();
  }

  [[nodiscard]] Expected<uint8_t> readU8() {
    return readUnsigned<uint8_t>();
  }

  [[nodiscard]] Expected<int16_t> readI16() {
    return readSigned<int16_t, uint16_t>();
  }

  [[nodiscard]] Expected<uint16_t> readU16() {
    return readUnsigned<uint16_t>();
  }

  [[nodiscard]] Expected<int32_t> readI32() {
    return readSigned<int32_t, uint32_t>();
  }

  [[nodiscard]] Expected<uint32_t> readU32() {
    return readUnsigned<uint32_t>();
  }

  [[nodiscard]] Expected<int64_t> readI64() {
    return readSigned<int64_t, uint64_t>();
  }

  [[nodiscard]] Expected<uint64_t> readU64() {
    return readUnsigned<uint64_t>();
  }

  [[nodiscard]] Expected<float> readF32() {
    auto bits = readU32();
    if (!bits) {
      return bits.status();
    }
    float value = 0;
    const uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  [[nodiscard]] Expected<double> readF64() {
    auto bits = readU64();
    if (!bits) {
      return bits.status();
    }
    double value = 0;
    const uint64_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  [[nodiscard]] Expected<std::string_view> readString() {
    auto length = readU32();
    if (!length) {
      return length.status();
    }
    if (*length == 0) {
      return fail("CDR string length does not include a terminator");
    }
    if (static_cast<uint64_t>(*length) > remaining()) {
      return fail("CDR string length exceeds the remaining payload");
    }
    const size_t size = *length;
    const char* chars = reinterpret_cast<const char*>(payload_.data + position_);
    if (chars[size - 1] != '\0') {
      return fail("CDR string is not NUL terminated");
    }
    position_ += size;
    return std::string_view(chars, size - 1);
  }

  [[nodiscard]] Expected<uint32_t> readSequenceLength(size_t minimum_element_size = 1) {
    auto count = readU32();
    if (!count) {
      return count.status();
    }
    if (minimum_element_size == 0) {
      return fail("CDR sequence element size must be nonzero");
    }
    if (static_cast<uint64_t>(*count) > remaining() / minimum_element_size) {
      return fail("CDR sequence length exceeds the remaining payload");
    }
    return *count;
  }

  [[nodiscard]] Expected<ByteView> readByteSequence() {
    auto count = readSequenceLength(1);
    if (!count) {
      return count.status();
    }
    const size_t size = *count;
    ByteView result(payload_.data + position_, size);
    position_ += size;
    return result;
  }

  template <typename T, size_t Size>
  [[nodiscard]] Status readFixedArray(std::array<T, Size>& output) {
    for (auto& value : output) {
      auto next = readPrimitive<T>();
      if (!next) {
        return next.status();
      }
      value = *next;
    }
    return Status::ok();
  }

  [[nodiscard]] Status enterStruct() {
    if (depth_ == kMaxTraversalDepth) {
      return fail("CDR traversal depth exceeds 64");
    }
    ++depth_;
    return Status::ok();
  }

  [[nodiscard]] Status leaveStruct() {
    if (depth_ == 0) {
      return fail("CDR struct-depth underflow");
    }
    --depth_;
    return Status::ok();
  }

  [[nodiscard]] Expected<uint32_t> u32(CdrFieldId field);
  [[nodiscard]] Expected<bool> boolean(CdrFieldId field);
  [[nodiscard]] Expected<int8_t> i8(CdrFieldId field);
  [[nodiscard]] Expected<uint8_t> u8(CdrFieldId field);
  [[nodiscard]] Expected<int16_t> i16(CdrFieldId field);
  [[nodiscard]] Expected<uint16_t> u16(CdrFieldId field);
  [[nodiscard]] Expected<int32_t> i32(CdrFieldId field);
  [[nodiscard]] Expected<uint64_t> u64(CdrFieldId field);
  [[nodiscard]] Expected<int64_t> i64(CdrFieldId field);
  [[nodiscard]] Expected<float> f32(CdrFieldId field);
  [[nodiscard]] Expected<double> f64(CdrFieldId field);
  [[nodiscard]] Expected<std::string_view> string(CdrFieldId field);
  [[nodiscard]] Expected<ByteView> bytes(CdrFieldId field);
  [[nodiscard]] Expected<InputSpanRef> spanRef(CdrFieldId field);

  /// Internal cache vocabulary exposed only so the header-only traversal plan
  /// can remain a separate type without dynamic polymorphism.
  enum class CachedKind : uint8_t {
    kUnknown,
    kBool,
    kI8,
    kU8,
    kI16,
    kU16,
    kI32,
    kU32,
    kI64,
    kU64,
    kF32,
    kF64,
    kString,
    kBytes,
  };

  struct CachedField {
    CachedKind kind = CachedKind::kUnknown;
    size_t offset = 0;
    size_t size = 0;
    bool located = false;
  };

 private:
  void initialize() {
    if (payload_.data == nullptr || payload_.size < 4) {
      status_ = Status::error("truncated CDR encapsulation header");
      return;
    }
    const uint16_t representation =
        static_cast<uint16_t>((static_cast<uint16_t>(payload_.data[0]) << 8) | payload_.data[1]);
    if (representation != 0 && representation != 1) {
      status_ = Status::error("unsupported CDR representation; XCDR1 plain CDR is required");
      return;
    }
    little_endian_ = representation == 1;
    data_origin_ = 4;
    position_ = data_origin_;
  }

  [[nodiscard]] size_t remaining() const noexcept {
    return position_ <= payload_.size ? payload_.size - position_ : 0;
  }

  [[nodiscard]] Status fail(std::string_view message) noexcept {
    status_ = Status::error(message);
    return status_;
  }

  template <typename UInt>
  [[nodiscard]] Expected<UInt> readUnsigned() {
    static_assert(std::is_unsigned<UInt>::value, "UInt must be unsigned");
    Status aligned = align(sizeof(UInt));
    if (!aligned.isOk()) {
      return aligned;
    }
    if (sizeof(UInt) > remaining()) {
      return fail("truncated CDR primitive");
    }
    UInt value = 0;
    if (little_endian_) {
      for (size_t index = 0; index < sizeof(UInt); ++index) {
        value |= static_cast<UInt>(payload_.data[position_ + index]) << (index * 8);
      }
    } else {
      for (size_t index = 0; index < sizeof(UInt); ++index) {
        value = static_cast<UInt>((value << 8) | payload_.data[position_ + index]);
      }
    }
    position_ += sizeof(UInt);
    return value;
  }

  template <typename Signed, typename Unsigned>
  [[nodiscard]] Expected<Signed> readSigned() {
    auto bits = readUnsigned<Unsigned>();
    if (!bits) {
      return bits.status();
    }
    Signed value = 0;
    const Unsigned raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  template <typename T>
  [[nodiscard]] Expected<T> readPrimitive() {
    if constexpr (std::is_same<T, bool>::value) {
      return readBool();
    } else if constexpr (std::is_same<T, int8_t>::value) {
      return readI8();
    } else if constexpr (std::is_same<T, uint8_t>::value) {
      return readU8();
    } else if constexpr (std::is_same<T, int16_t>::value) {
      return readI16();
    } else if constexpr (std::is_same<T, uint16_t>::value) {
      return readU16();
    } else if constexpr (std::is_same<T, int32_t>::value) {
      return readI32();
    } else if constexpr (std::is_same<T, uint32_t>::value) {
      return readU32();
    } else if constexpr (std::is_same<T, int64_t>::value) {
      return readI64();
    } else if constexpr (std::is_same<T, uint64_t>::value) {
      return readU64();
    } else if constexpr (std::is_same<T, float>::value) {
      return readF32();
    } else if constexpr (std::is_same<T, double>::value) {
      return readF64();
    } else {
      static_assert(!std::is_same<T, T>::value, "unsupported CDR primitive type");
    }
  }

  [[nodiscard]] Status ensurePlannedFields();
  [[nodiscard]] Expected<CachedField> cached(CdrFieldId field, CachedKind expected);
  [[nodiscard]] Expected<uint64_t> cachedUnsigned(CdrFieldId field, CachedKind kind, size_t width);

  /// Reinterpret a cached unsigned field of width sizeof(Bits) as Value. The
  /// cached-field counterpart of readSigned().
  template <typename Value, typename Bits>
  [[nodiscard]] Expected<Value> cachedBits(CdrFieldId field, CachedKind kind) {
    auto value = cachedUnsigned(field, kind, sizeof(Bits));
    if (!value) {
      return value.status();
    }
    const Bits bits = static_cast<Bits>(*value);
    Value result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
  }

  PayloadView payload_;
  Status status_;
  bool little_endian_ = false;
  size_t data_origin_ = 0;
  size_t position_ = 0;
  size_t depth_ = 0;
  const CdrTraversalPlan* plan_ = nullptr;
  std::vector<CachedField> cached_fields_;
  size_t traversal_count_ = 0;

  friend class CdrTraversalPlan;
};

}  // namespace pj
