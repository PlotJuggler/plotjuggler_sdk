#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace PJ {

/// Inclusive min/max pair for scalar ranges.
template <typename T>
struct Range {
  T min = std::numeric_limits<T>::lowest();
  T max = std::numeric_limits<T>::max();

  /// Normalize this range in place so min <= max.
  constexpr void normalize() noexcept {
    if (max < min) {
      std::swap(min, max);
    }
  }

  /// Return a normalized copy of this range.
  [[nodiscard]] constexpr Range normalized() const noexcept {
    Range copy = *this;
    copy.normalize();
    return copy;
  }

  /// Clamp a value to this inclusive range.
  [[nodiscard]] constexpr T clamp(const T& value) const {
    return std::clamp(value, min, max);
  }

  /// Return true if a value is inside this inclusive range.
  [[nodiscard]] constexpr bool contains(const T& value) const noexcept {
    if constexpr (std::is_floating_point_v<T>) {
      if (value != value) {
        return false;
      }
    }
    return !(value < min) && !(max < value);
  }

  /// Return true if either side is bounded away from the default full range.
  [[nodiscard]] constexpr bool hasLimits() const noexcept {
    return min != std::numeric_limits<T>::lowest() || max != std::numeric_limits<T>::max();
  }

  [[nodiscard]] constexpr bool operator==(const Range&) const = default;
};

/// Stable identifier for a dataset.
using DatasetId = uint32_t;
/// Stable identifier for a topic.
using TopicId = uint32_t;
/// Stable identifier for a flattened field/column.
using FieldId = uint32_t;
/// Stable identifier for a sealed chunk.
using ChunkId = uint64_t;
/// Stable identifier for a time domain.
using TimeDomainId = uint32_t;
/// Stable identifier for a registered schema.
using SchemaId = uint32_t;
/// Stable identifier for a plugin instance.
using PluginId = uint32_t;

/// Timestamp in nanoseconds since the Unix epoch (1970-01-01T00:00:00Z).
using Timestamp = int64_t;  // nanoseconds since Unix epoch

/// Numeric scalar kind used by scalar APIs.
enum class NumericType : uint8_t {
  kFloat32,
  kFloat64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
};

/// Runtime numeric payload used by scalar APIs.
using NumericValue =
    std::variant<float, double, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t>;

// Size in bytes of each numeric type
[[nodiscard]] constexpr size_t numericTypeSize(NumericType type) noexcept {
  switch (type) {
    case NumericType::kFloat32:
      return sizeof(float);
    case NumericType::kFloat64:
      return sizeof(double);
    case NumericType::kInt8:
      return sizeof(int8_t);
    case NumericType::kInt16:
      return sizeof(int16_t);
    case NumericType::kInt32:
      return sizeof(int32_t);
    case NumericType::kInt64:
      return sizeof(int64_t);
    case NumericType::kUint8:
      return sizeof(uint8_t);
    case NumericType::kUint16:
      return sizeof(uint16_t);
    case NumericType::kUint32:
      return sizeof(uint32_t);
    case NumericType::kUint64:
      return sizeof(uint64_t);
  }
  return 0;  // unreachable
}

// Map a NumericValue variant index to NumericType
[[nodiscard]] constexpr NumericType numericValueType(const NumericValue& v) noexcept {
  return static_cast<NumericType>(v.index());
}

// Convert any NumericValue to double (for stats, display)
[[nodiscard]] constexpr double numericValueToDouble(const NumericValue& v) noexcept {
  return std::visit([](const auto& val) -> double { return static_cast<double>(val); }, v);
}

/// Stable identifier for a derived DAG node.
using NodeId = uint32_t;

/// Sentinel value for an invalid/uninitialized chunk id.
constexpr ChunkId kInvalidChunkId = 0;

/// Sentinel value for an invalid/uninitialized node id.
constexpr NodeId kInvalidNodeId = 0;

/// Sentinel type representing a null/missing value in a variant.
struct NullValue {};
constexpr NullValue kNull;

}  // namespace PJ
